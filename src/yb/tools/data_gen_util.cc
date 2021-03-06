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
#include "yb/tools/data_gen_util.h"

#include "yb/client/schema.h"
#include "yb/common/partial_row.h"
#include "yb/gutil/strings/numbers.h"
#include "yb/util/random.h"
#include "yb/util/status.h"

namespace yb {
namespace tools {

void WriteValueToColumn(const client::YBSchema& schema,
                        int col_idx,
                        uint64_t value,
                        YBPartialRow* row) {
  DataType type = schema.Column(col_idx).type()->main();
  char buf[kFastToBufferSize];
  switch (type) {
    case INT8:
      CHECK_OK(row->SetInt8(col_idx, value));
      break;
    case INT16:
      CHECK_OK(row->SetInt16(col_idx, value));
      break;
    case INT32:
      CHECK_OK(row->SetInt32(col_idx, value));
      break;
    case INT64:
      CHECK_OK(row->SetInt64(col_idx, value));
      break;
    case FLOAT:
      CHECK_OK(row->SetFloat(col_idx, value / 123.0));
      break;
    case DOUBLE:
      CHECK_OK(row->SetDouble(col_idx, value / 123.0));
      break;
    case STRING:
      CHECK_OK(row->SetStringCopy(col_idx, FastHex64ToBuffer(value, buf)));
      break;
    case BOOL:
      CHECK_OK(row->SetBool(col_idx, value));
      break;
    default:
      LOG(FATAL) << "Unexpected data type: " << type;
  }
}

void GenerateDataForRow(const client::YBSchema& schema, uint64_t record_id,
                        Random* random, YBPartialRow* row) {
  for (int col_idx = 0; col_idx < schema.num_columns(); col_idx++) {
    // We randomly generate the inserted data, except for the first column,
    // which is always based on a monotonic "record id".
    uint64_t value;
    if (col_idx == 0) {
      value = record_id;
    } else {
      value = random->Next64();
    }
    WriteValueToColumn(schema, col_idx, value, row);
  }
}

} // namespace tools
} // namespace yb
