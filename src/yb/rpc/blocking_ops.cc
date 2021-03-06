// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//

#include "yb/rpc/blocking_ops.h"

#include <stdint.h>

#include <google/protobuf/message_lite.h>

#include <glog/logging.h>

#include "yb/gutil/endian.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/rpc/constants.h"
#include "yb/rpc/serialization.h"
#include "yb/util/faststring.h"
#include "yb/util/monotime.h"
#include "yb/util/net/sockaddr.h"
#include "yb/util/net/socket.h"
#include "yb/util/ref_cnt_buffer.h"
#include "yb/util/status.h"

DECLARE_int32(rpc_max_message_size);

namespace yb {
namespace rpc {

using google::protobuf::MessageLite;

Status EnsureBlockingMode(const Socket* const sock) {
  bool is_nonblocking;
  RETURN_NOT_OK(sock->IsNonBlocking(&is_nonblocking));
  if (is_nonblocking) {
    return STATUS(IllegalState, "Underlying socket is not set to blocking mode!");
  }
  return Status::OK();
}

Status SendFramedMessageBlocking(Socket* sock, const MessageLite& header, const MessageLite& msg,
    const MonoTime& deadline) {
  DCHECK(sock != nullptr);
  DCHECK(header.IsInitialized()) << "header protobuf must be initialized";
  DCHECK(msg.IsInitialized()) << "msg protobuf must be initialized";

  RETURN_NOT_OK(EnsureBlockingMode(sock));

  // Ensure we are in blocking mode.
  // These blocking calls are typically not in the fast path, so doing this for all build types.
  bool is_non_blocking = false;
  RETURN_NOT_OK(sock->IsNonBlocking(&is_non_blocking));
  DCHECK(!is_non_blocking) << "Socket must be in blocking mode to use SendFramedMessage";

  // Serialize message
  RefCntBuffer param_buf;
  RETURN_NOT_OK(serialization::SerializeMessage(msg, &param_buf));

  // Serialize header and initial length
  RefCntBuffer header_buf;
  RETURN_NOT_OK(serialization::SerializeHeader(header, param_buf.size(), &header_buf));

  // Write header & param to stream
  size_t nsent;
  RETURN_NOT_OK(sock->BlockingWrite(header_buf.udata(), header_buf.size(), &nsent, deadline));
  RETURN_NOT_OK(sock->BlockingWrite(param_buf.udata(), param_buf.size(), &nsent, deadline));

  return Status::OK();
}

Status ReceiveFramedMessageBlocking(Socket* sock, faststring* recv_buf,
    MessageLite* header, Slice* param_buf, const MonoTime& deadline) {
  DCHECK(sock != nullptr);
  DCHECK(recv_buf != nullptr);
  DCHECK(header != nullptr);
  DCHECK(param_buf != nullptr);

  RETURN_NOT_OK(EnsureBlockingMode(sock));

  // Read the message prefix, which specifies the length of the payload.
  recv_buf->clear();
  recv_buf->resize(kMsgLengthPrefixLength);
  size_t recvd = 0;
  RETURN_NOT_OK(sock->BlockingRecv(recv_buf->data(), kMsgLengthPrefixLength, &recvd, deadline));
  uint32_t payload_len = NetworkByteOrder::Load32(recv_buf->data());

  // Verify that the payload size isn't out of bounds.
  // This can happen because of network corruption, or a naughty client.
  if (PREDICT_FALSE(payload_len > FLAGS_rpc_max_message_size)) {
    return STATUS(IOError,
        strings::Substitute(
            "Received invalid message of size $0 which exceeds"
            " the rpc_max_message_size of $1 bytes",
            payload_len, FLAGS_rpc_max_message_size));
  }

  // Read the message payload.
  recvd = 0;
  recv_buf->resize(payload_len + kMsgLengthPrefixLength);
  RETURN_NOT_OK(sock->BlockingRecv(recv_buf->data() + kMsgLengthPrefixLength,
                payload_len, &recvd, deadline));
  Slice slice(recv_buf->data() + kMsgLengthPrefixLength, payload_len);
  RETURN_NOT_OK(serialization::ParseYBMessage(slice, header, param_buf));
  return Status::OK();
}

} // namespace rpc
} // namespace yb
