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

#include "yb/common/wire_protocol.h"

#include <string>
#include <vector>

#include "yb/common/row.h"
#include "yb/common/rowblock.h"
#include "yb/gutil/port.h"
#include "yb/gutil/stl_util.h"
#include "yb/gutil/strings/fastmem.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/util/faststring.h"
#include "yb/util/logging.h"
#include "yb/util/net/net_util.h"
#include "yb/util/net/sockaddr.h"
#include "yb/util/safe_math.h"
#include "yb/util/slice.h"

using google::protobuf::RepeatedPtrField;
using std::vector;

namespace yb {

void StatusToPB(const Status& status, AppStatusPB* pb) {
  pb->Clear();
  bool is_unknown = false;
  if (status.ok()) {
    pb->set_code(AppStatusPB::OK);
    // OK statuses don't have any message or posix code.
    return;
  } else if (status.IsNotFound()) {
    pb->set_code(AppStatusPB::NOT_FOUND);
  } else if (status.IsCorruption()) {
    pb->set_code(AppStatusPB::CORRUPTION);
  } else if (status.IsNotSupported()) {
    pb->set_code(AppStatusPB::NOT_SUPPORTED);
  } else if (status.IsInvalidArgument()) {
    pb->set_code(AppStatusPB::INVALID_ARGUMENT);
  } else if (status.IsIOError()) {
    pb->set_code(AppStatusPB::IO_ERROR);
  } else if (status.IsAlreadyPresent()) {
    pb->set_code(AppStatusPB::ALREADY_PRESENT);
  } else if (status.IsRuntimeError()) {
    pb->set_code(AppStatusPB::RUNTIME_ERROR);
  } else if (status.IsNetworkError()) {
    pb->set_code(AppStatusPB::NETWORK_ERROR);
  } else if (status.IsIllegalState()) {
    pb->set_code(AppStatusPB::ILLEGAL_STATE);
  } else if (status.IsNotAuthorized()) {
    pb->set_code(AppStatusPB::NOT_AUTHORIZED);
  } else if (status.IsAborted()) {
    pb->set_code(AppStatusPB::ABORTED);
  } else if (status.IsRemoteError()) {
    pb->set_code(AppStatusPB::REMOTE_ERROR);
  } else if (status.IsServiceUnavailable()) {
    pb->set_code(AppStatusPB::SERVICE_UNAVAILABLE);
  } else if (status.IsTimedOut()) {
    pb->set_code(AppStatusPB::TIMED_OUT);
  } else if (status.IsUninitialized()) {
    pb->set_code(AppStatusPB::UNINITIALIZED);
  } else if (status.IsConfigurationError()) {
    pb->set_code(AppStatusPB::CONFIGURATION_ERROR);
  } else if (status.IsIncomplete()) {
    pb->set_code(AppStatusPB::INCOMPLETE);
  } else if (status.IsEndOfFile()) {
    pb->set_code(AppStatusPB::END_OF_FILE);
  } else if (status.IsInvalidCommand()) {
    pb->set_code(AppStatusPB::INVALID_COMMAND);
  } else if (status.IsQLError()) {
    pb->set_code(AppStatusPB::SQL_ERROR);
  } else if (status.IsInternalError()) {
    pb->set_code(AppStatusPB::INTERNAL_ERROR);
  } else if (status.IsExpired()) {
    pb->set_code(AppStatusPB::EXPIRED);
  } else if (status.IsLeaderHasNoLease()) {
    pb->set_code(AppStatusPB::LEADER_HAS_NO_LEASE);
  } else if (status.IsLeaderNotReadyToServe()) {
    pb->set_code(AppStatusPB::LEADER_NOT_READY_TO_SERVE);
  } else if (status.IsTryAgain()) {
    pb->set_code(AppStatusPB::TRY_AGAIN_CODE);
  } else {
    LOG(WARNING) << "Unknown error code translation from internal error "
                 << status.ToString() << ": sending UNKNOWN_ERROR";
    pb->set_code(AppStatusPB::UNKNOWN_ERROR);
    is_unknown = true;
  }
  if (is_unknown) {
    // For unknown status codes, include the original stringified error
    // code.
    pb->set_message(status.CodeAsString() + ": " +
                    status.message().ToString());
  } else {
    // Otherwise, just encode the message itself, since the other end
    // will reconstruct the other parts of the ToString() response.
    pb->set_message(status.message().ToString());
  }
  if (status.IsQLError()) {
    pb->set_ql_error_code(status.ql_error_code());
  } else if (status.posix_code() != -1) {
    pb->set_posix_code(status.posix_code());
  }
}

Status StatusFromPB(const AppStatusPB& pb) {
  int posix_code = pb.has_posix_code() ? pb.posix_code() : -1;

  switch (pb.code()) {
    case AppStatusPB::OK:
      return Status::OK();
    case AppStatusPB::NOT_FOUND:
      return STATUS(NotFound, pb.message(), "", posix_code);
    case AppStatusPB::CORRUPTION:
      return STATUS(Corruption, pb.message(), "", posix_code);
    case AppStatusPB::NOT_SUPPORTED:
      return STATUS(NotSupported, pb.message(), "", posix_code);
    case AppStatusPB::INVALID_ARGUMENT:
      return STATUS(InvalidArgument, pb.message(), "", posix_code);
    case AppStatusPB::IO_ERROR:
      return STATUS(IOError, pb.message(), "", posix_code);
    case AppStatusPB::ALREADY_PRESENT:
      return STATUS(AlreadyPresent, pb.message(), "", posix_code);
    case AppStatusPB::RUNTIME_ERROR:
      return STATUS(RuntimeError, pb.message(), "", posix_code);
    case AppStatusPB::NETWORK_ERROR:
      return STATUS(NetworkError, pb.message(), "", posix_code);
    case AppStatusPB::ILLEGAL_STATE:
      return STATUS(IllegalState, pb.message(), "", posix_code);
    case AppStatusPB::NOT_AUTHORIZED:
      return STATUS(NotAuthorized, pb.message(), "", posix_code);
    case AppStatusPB::ABORTED:
      return STATUS(Aborted, pb.message(), "", posix_code);
    case AppStatusPB::REMOTE_ERROR:
      return STATUS(RemoteError, pb.message(), "", posix_code);
    case AppStatusPB::SERVICE_UNAVAILABLE:
      return STATUS(ServiceUnavailable, pb.message(), "", posix_code);
    case AppStatusPB::TIMED_OUT:
      return STATUS(TimedOut, pb.message(), "", posix_code);
    case AppStatusPB::UNINITIALIZED:
      return STATUS(Uninitialized, pb.message(), "", posix_code);
    case AppStatusPB::CONFIGURATION_ERROR:
      return STATUS(ConfigurationError, pb.message(), "", posix_code);
    case AppStatusPB::INCOMPLETE:
      return STATUS(Incomplete, pb.message(), "", posix_code);
    case AppStatusPB::END_OF_FILE:
      return STATUS(EndOfFile, pb.message(), "", posix_code);
    case AppStatusPB::INVALID_COMMAND:
      return STATUS(InvalidCommand, pb.message(), "", posix_code);
    case AppStatusPB::SQL_ERROR:
      if (!pb.has_ql_error_code()) {
        return STATUS(InternalError, "SQL error code missing");
      }
      return STATUS(QLError, pb.message(), "", pb.ql_error_code());
    case AppStatusPB::INTERNAL_ERROR:
      return STATUS(InternalError, pb.message(), "", posix_code);
    case AppStatusPB::EXPIRED:
      return STATUS(Expired, pb.message(), "", posix_code);
    case AppStatusPB::LEADER_HAS_NO_LEASE:
      return STATUS(LeaderHasNoLease, pb.message(), "", posix_code);
    case AppStatusPB::LEADER_NOT_READY_TO_SERVE:
      return STATUS(LeaderNotReadyToServe, pb.message(), "", posix_code);
    case AppStatusPB::TRY_AGAIN_CODE:
      return STATUS(TryAgain, pb.message(), "", posix_code);
    case AppStatusPB::UNKNOWN_ERROR:
    default:
      LOG(WARNING) << "Unknown error code in status: " << pb.ShortDebugString();
      return STATUS(RuntimeError, "(unknown error code)", pb.message(), posix_code);
  }
}

Status HostPortToPB(const HostPort& host_port, HostPortPB* host_port_pb) {
  host_port_pb->set_host(host_port.host());
  host_port_pb->set_port(host_port.port());
  return Status::OK();
}

Status HostPortFromPB(const HostPortPB& host_port_pb, HostPort* host_port) {
  host_port->set_host(host_port_pb.host());
  host_port->set_port(host_port_pb.port());
  return Status::OK();
}

Status EndpointFromHostPortPB(const HostPortPB& host_portpb, Endpoint* endpoint) {
  HostPort host_port;
  RETURN_NOT_OK(HostPortFromPB(host_portpb, &host_port));
  return EndpointFromHostPort(host_port, endpoint);
}

Status HostPortsToPBs(const std::vector<HostPort>& addrs,
                      RepeatedPtrField<HostPortPB>* pbs) {
  for (const auto& addr : addrs) {
    RETURN_NOT_OK(HostPortToPB(addr, pbs->Add()));
  }
  return Status::OK();
}

Status AddHostPortPBs(const std::vector<Endpoint>& addrs,
                      RepeatedPtrField<HostPortPB>* pbs) {
  for (const auto& addr : addrs) {
    HostPortPB* pb = pbs->Add();
    pb->set_port(addr.port());
    if (addr.address().is_unspecified()) {
      auto status = GetFQDN(pb->mutable_host());
      if (!status.ok()) {
        std::vector<IpAddress> locals;
        if (!GetLocalAddresses(&locals, AddressFilter::EXTERNAL).ok() || locals.empty()) {
          return status;
        }
        for (auto& address : locals) {
          if (pb == nullptr) {
            pb = pbs->Add();
            pb->set_port(addr.port());
          }
          pb->set_host(address.to_string());
          pb = nullptr;
        }
      }
    } else {
      pb->set_host(addr.address().to_string());
    }
  }
  return Status::OK();
}

Status SchemaToPB(const Schema& schema, SchemaPB *pb, int flags) {
  pb->Clear();
  RETURN_NOT_OK(SchemaToColumnPBs(schema, pb->mutable_columns(), flags));
  schema.table_properties().ToTablePropertiesPB(pb->mutable_table_properties());
  return Status::OK();
}

Status SchemaToPBWithoutIds(const Schema& schema, SchemaPB *pb) {
  pb->Clear();
  return SchemaToColumnPBs(schema, pb->mutable_columns(), SCHEMA_PB_WITHOUT_IDS);
}

Status SchemaFromPB(const SchemaPB& pb, Schema *schema) {
  // Conver the columns.
  vector<ColumnSchema> columns;
  vector<ColumnId> column_ids;
  int num_key_columns = 0;
  RETURN_NOT_OK(ColumnPBsToColumnTuple(pb.columns(), &columns, &column_ids, &num_key_columns));

  // Convert the table properties.
  TableProperties table_properties = TableProperties::FromTablePropertiesPB(pb.table_properties());
  return schema->Reset(columns, column_ids, num_key_columns, table_properties);
}

void ColumnSchemaToPB(const ColumnSchema& col_schema, ColumnSchemaPB *pb, int flags) {
  pb->Clear();
  pb->set_name(col_schema.name());
  col_schema.type()->ToQLTypePB(pb->mutable_type());
  pb->set_is_nullable(col_schema.is_nullable());
  pb->set_is_static(col_schema.is_static());
  pb->set_is_counter(col_schema.is_counter());
  pb->set_sorting_type(col_schema.sorting_type());
  // We only need to process the *hash* primary key here. The regular primary key is set by the
  // conversion for SchemaPB. The reason is that ColumnSchema and ColumnSchemaPB are not matching
  // 1 to 1 as ColumnSchema doesn't have "is_key" field. That was Kudu's code, and we keep it that
  // way for now.
  if (col_schema.is_hash_key()) {
    pb->set_is_key(true);
    pb->set_is_hash_key(true);
  }
  if (!(flags & SCHEMA_PB_WITHOUT_STORAGE_ATTRIBUTES)) {
    pb->set_encoding(col_schema.attributes().encoding);
    pb->set_compression(col_schema.attributes().compression);
    pb->set_cfile_block_size(col_schema.attributes().cfile_block_size);
  }
  if (col_schema.has_read_default()) {
    if (col_schema.type_info()->physical_type() == BINARY) {
      const Slice *read_slice = static_cast<const Slice *>(col_schema.read_default_value());
      pb->set_read_default_value(read_slice->data(), read_slice->size());
    } else {
      const void *read_value = col_schema.read_default_value();
      pb->set_read_default_value(read_value, col_schema.type_info()->size());
    }
  }
  if (col_schema.has_write_default()) {
    if (col_schema.type_info()->physical_type() == BINARY) {
      const Slice *write_slice = static_cast<const Slice *>(col_schema.write_default_value());
      pb->set_write_default_value(write_slice->data(), write_slice->size());
    } else {
      const void *write_value = col_schema.write_default_value();
      pb->set_write_default_value(write_value, col_schema.type_info()->size());
    }
  }
}

ColumnSchema ColumnSchemaFromPB(const ColumnSchemaPB& pb) {
  const void *write_default_ptr = nullptr;
  const void *read_default_ptr = nullptr;
  Slice write_default;
  Slice read_default;
  const TypeInfo* typeinfo = GetTypeInfo(pb.type().main());
  if (pb.has_read_default_value()) {
    read_default = Slice(pb.read_default_value());
    if (typeinfo->physical_type() == BINARY) {
      read_default_ptr = &read_default;
    } else {
      read_default_ptr = read_default.data();
    }
  }
  if (pb.has_write_default_value()) {
    write_default = Slice(pb.write_default_value());
    if (typeinfo->physical_type() == BINARY) {
      write_default_ptr = &write_default;
    } else {
      write_default_ptr = write_default.data();
    }
  }

  ColumnStorageAttributes attributes;
  if (pb.has_encoding()) {
    attributes.encoding = pb.encoding();
  }
  if (pb.has_compression()) {
    attributes.compression = pb.compression();
  }
  if (pb.has_cfile_block_size()) {
    attributes.cfile_block_size = pb.cfile_block_size();
  }
  // Only "is_hash_key" is used to construct ColumnSchema. The field "is_key" will be read when
  // processing SchemaPB.
  return ColumnSchema(pb.name(), QLType::FromQLTypePB(pb.type()), pb.is_nullable(),
                      pb.is_hash_key(), pb.is_static(), pb.is_counter(),
                      ColumnSchema::SortingType(pb.sorting_type()),
                      read_default_ptr, write_default_ptr, attributes);
}

CHECKED_STATUS ColumnPBsToColumnTuple(
    const RepeatedPtrField<ColumnSchemaPB>& column_pbs,
    vector<ColumnSchema>* columns , vector<ColumnId>* column_ids, int* num_key_columns) {
  columns->reserve(column_pbs.size());
  bool is_handling_key = true;
  for (const ColumnSchemaPB& pb : column_pbs) {
    columns->push_back(ColumnSchemaFromPB(pb));
    if (pb.is_key()) {
      if (!is_handling_key) {
        return STATUS(InvalidArgument,
                      "Got out-of-order key column", pb.ShortDebugString());
      }
      (*num_key_columns)++;
    } else {
      is_handling_key = false;
    }
    if (pb.has_id()) {
      column_ids->push_back(ColumnId(pb.id()));
    }
  }

  DCHECK_LE((*num_key_columns), columns->size());
  return Status::OK();
}

Status ColumnPBsToSchema(const RepeatedPtrField<ColumnSchemaPB>& column_pbs,
                         Schema* schema) {

  vector<ColumnSchema> columns;
  vector<ColumnId> column_ids;
  int num_key_columns = 0;
  RETURN_NOT_OK(ColumnPBsToColumnTuple(column_pbs, &columns, &column_ids, &num_key_columns));

  // TODO(perf): could make the following faster by adding a
  // Reset() variant which actually takes ownership of the column
  // vector.
  return schema->Reset(columns, column_ids, num_key_columns);
}

Status SchemaToColumnPBs(const Schema& schema,
                         RepeatedPtrField<ColumnSchemaPB>* cols,
                         int flags) {
  cols->Clear();
  int idx = 0;
  for (const ColumnSchema& col : schema.columns()) {
    ColumnSchemaPB* col_pb = cols->Add();
    ColumnSchemaToPB(col, col_pb);
    col_pb->set_is_key(idx < schema.num_key_columns());

    if (schema.has_column_ids() && !(flags & SCHEMA_PB_WITHOUT_IDS)) {
      col_pb->set_id(schema.column_id(idx));
    }

    idx++;
  }
  return Status::OK();
}

// Because we use a faststring here, ASAN tests become unbearably slow
// with the extra verifications.
ATTRIBUTE_NO_ADDRESS_SAFETY_ANALYSIS
Status RewriteRowBlockPointers(const Schema& schema, const RowwiseRowBlockPB& rowblock_pb,
                               const Slice& indirect_data_slice, Slice* row_data_slice) {
  // TODO: cheating here so we can rewrite the request as it arrived and
  // change any indirect data pointers back to "real" pointers instead of
  // on-the-wire pointers. Maybe the RPC layer should give us a non-const
  // request? Maybe we should suck it up and copy the data when we mutate?

  // We don't need a const-cast because we can just use Slice's lack of
  // const-safety.
  uint8_t* row_data = row_data_slice->mutable_data();
  const uint8_t* indir_data = indirect_data_slice.data();
  size_t row_size = ContiguousRowHelper::row_size(schema);
  size_t expected_data_size = rowblock_pb.num_rows() * row_size;

  if (PREDICT_FALSE(row_data_slice->size() != expected_data_size)) {
    return STATUS(Corruption,
      StringPrintf("Row block has %zd bytes of data but expected %zd for %" PRIu32 " rows",
                   row_data_slice->size(), expected_data_size, rowblock_pb.num_rows()));
  }

  for (int i = 0; i < schema.num_columns(); i++) {
    const ColumnSchema& col = schema.column(i);
    if (col.type_info()->physical_type() != BINARY) {
      continue;
    }

    int row_idx = 0;
    size_t offset = 0;
    while (offset < row_data_slice->size()) {
      ContiguousRow row(&schema, &row_data[offset]);
      uint8_t* dst_cell = row.mutable_cell_ptr(i);

      if (!col.is_nullable() || !row.is_null(i)) {
        // The pointer is currently an offset into indir_data. Need to replace it
        // with the actual pointer into indir_data
        Slice *slice = reinterpret_cast<Slice *>(dst_cell);
        size_t offset_in_indirect = reinterpret_cast<uintptr_t>(slice->data());

        // Ensure the updated pointer is within the bounds of the indirect data.
        bool overflowed = false;
        size_t max_offset = AddWithOverflowCheck(offset_in_indirect, slice->size(), &overflowed);
        if (PREDICT_FALSE(overflowed || max_offset > indirect_data_slice.size())) {
          return STATUS(Corruption,
            StringPrintf("Row #%d contained bad indirect slice for column %s: (%zd, %zd)",
                         row_idx, col.ToString().c_str(),
                         reinterpret_cast<uintptr_t>(slice->data()),
                         slice->size()));
        }
        *slice = Slice(&indir_data[offset_in_indirect], slice->size());
      }

      // Advance to next row
      offset += row_size;
      row_idx++;
    }
  }

  return Status::OK();
}

Status ExtractRowsFromRowBlockPB(const Schema& schema,
                                 const RowwiseRowBlockPB& rowblock_pb,
                                 const Slice& indirect_data,
                                 Slice* rows_data,
                                 vector<const uint8_t*>* rows) {
  RETURN_NOT_OK(RewriteRowBlockPointers(schema, rowblock_pb, indirect_data, rows_data));

  int n_rows = rowblock_pb.num_rows();
  if (PREDICT_FALSE(n_rows == 0)) {
    // Early-out here to avoid a UBSAN failure.
    return Status::OK();
  }

  // Doing this resize and array indexing turns out to be noticeably faster
  // than using reserve and push_back.
  size_t row_size = ContiguousRowHelper::row_size(schema);
  const uint8_t* src = rows_data->data();
  int dst_index = rows->size();
  rows->resize(rows->size() + n_rows);
  const uint8_t** dst = &(*rows)[dst_index];
  while (n_rows > 0) {
    *dst++ = src;
    src += row_size;
    n_rows--;
  }

  return Status::OK();
}

Status FindLeaderHostPort(const RepeatedPtrField<ServerEntryPB>& entries,
                          HostPort* leader_hostport) {
  for (const ServerEntryPB& entry : entries) {
    if (entry.has_error()) {
      LOG(WARNING) << "Error encountered for server entry " << entry.ShortDebugString()
                   << ": " << StatusFromPB(entry.error()).ToString();
      continue;
    }
    if (!entry.has_role()) {
      return STATUS(IllegalState,
          strings::Substitute("Every server in must have a role, but entry ($0) has no role.",
                              entry.ShortDebugString()));
    }
    if (entry.role() == consensus::RaftPeerPB::LEADER) {
      return HostPortFromPB(entry.registration().rpc_addresses(0), leader_hostport);
    }
  }
  return STATUS(NotFound, "No leader found.");
}

template<class RowType>
void AppendRowToString(const RowType& row, string* buf);

template<>
void AppendRowToString<ConstContiguousRow>(const ConstContiguousRow& row, string* buf) {
  buf->append(reinterpret_cast<const char*>(row.row_data()), row.row_size());
}

template<>
void AppendRowToString<RowBlockRow>(const RowBlockRow& row, string* buf) {
  size_t row_size = ContiguousRowHelper::row_size(*row.schema());
  size_t appended_offset = buf->size();
  buf->resize(buf->size() + row_size);
  uint8_t* copied_rowdata = reinterpret_cast<uint8_t*>(&(*buf)[appended_offset]);
  ContiguousRow copied_row(row.schema(), copied_rowdata);
  CHECK_OK(CopyRow(row, &copied_row, reinterpret_cast<Arena*>(NULL)));
}

// Copy a column worth of data from the given RowBlock into the output
// protobuf.
//
// IS_NULLABLE: true if the column is nullable
// IS_VARLEN: true if the column is of variable length
//
// These are template parameters rather than normal function arguments
// so that there are fewer branches inside the loop.
//
// NOTE: 'dst_schema' must either be NULL or a subset of the specified's
// RowBlock's schema. If not NULL, then column at 'col_idx' in 'block' will
// be copied to column 'dst_col_idx' in the output protobuf; otherwise,
// dst_col_idx must be equal to col_idx.
template<bool IS_NULLABLE, bool IS_VARLEN>
static void CopyColumn(const RowBlock& block, int col_idx,
                       int dst_col_idx, uint8_t* dst_base,
                       faststring* indirect_data, const Schema* dst_schema) {
  DCHECK_ONLY_NOTNULL(dst_schema);
  ColumnBlock cblock = block.column_block(col_idx);
  size_t row_stride = ContiguousRowHelper::row_size(*dst_schema);
  uint8_t* dst = dst_base + dst_schema->column_offset(dst_col_idx);
  size_t offset_to_null_bitmap = dst_schema->byte_size() - dst_schema->column_offset(dst_col_idx);

  size_t cell_size = cblock.stride();
  const uint8_t* src = cblock.cell_ptr(0);

  BitmapIterator selected_row_iter(block.selection_vector()->bitmap(),
                                   block.nrows());
  int run_size;
  bool selected;
  int row_idx = 0;
  while ((run_size = selected_row_iter.Next(&selected))) {
    if (!selected) {
      src += run_size * cell_size;
      row_idx += run_size;
      continue;
    }
    for (int i = 0; i < run_size; i++) {
      if (IS_NULLABLE && cblock.is_null(row_idx)) {
        memset(dst, 0, cell_size);
        BitmapChange(dst + offset_to_null_bitmap, dst_col_idx, true);
      } else if (IS_VARLEN) {
        const Slice *slice = reinterpret_cast<const Slice *>(src);
        size_t offset_in_indirect = indirect_data->size();
        indirect_data->append(reinterpret_cast<const char*>(slice->data()),
                              slice->size());

        Slice *dst_slice = reinterpret_cast<Slice *>(dst);
        *dst_slice = Slice(reinterpret_cast<const uint8_t*>(offset_in_indirect),
                           slice->size());
        if (IS_NULLABLE) {
          BitmapChange(dst + offset_to_null_bitmap, dst_col_idx, false);
        }
      } else { // non-string, non-null
        strings::memcpy_inlined(dst, src, cell_size);
        if (IS_NULLABLE) {
          BitmapChange(dst + offset_to_null_bitmap, dst_col_idx, false);
        }
      }
      dst += row_stride;
      src += cell_size;
      row_idx++;
    }
  }
}

// Because we use a faststring here, ASAN tests become unbearably slow
// with the extra verifications.
ATTRIBUTE_NO_ADDRESS_SAFETY_ANALYSIS
void SerializeRowBlock(const RowBlock& block, RowwiseRowBlockPB* rowblock_pb,
                       const Schema* projection_schema,
                       faststring* data_buf, faststring* indirect_data) {
  DCHECK_GT(block.nrows(), 0);
  const Schema& tablet_schema = block.schema();

  if (projection_schema == nullptr) {
    projection_schema = &tablet_schema;
  }

  size_t old_size = data_buf->size();
  size_t row_stride = ContiguousRowHelper::row_size(*projection_schema);
  int num_rows = block.selection_vector()->CountSelected();
  data_buf->resize(old_size + row_stride * num_rows);
  uint8_t* base = reinterpret_cast<uint8_t*>(&(*data_buf)[old_size]);

  size_t proj_schema_idx = 0;
  for (int t_schema_idx = 0; t_schema_idx < tablet_schema.num_columns(); t_schema_idx++) {
    const ColumnSchema& col = tablet_schema.column(t_schema_idx);
    proj_schema_idx = projection_schema->find_column(col.name());
    if (proj_schema_idx == -1) {
      continue;
    }

    // Generating different functions for each of these cases makes them much less
    // branch-heavy -- we do the branch once outside the loop, and then have a
    // compiled version for each combination below.
    // TODO: Using LLVM to build a specialized CopyColumn on the fly should have
    // even bigger gains, since we could inline the constant cell sizes and column
    // offsets.
    if (col.is_nullable() && col.type_info()->physical_type() == BINARY) {
      CopyColumn<true, true>(block, t_schema_idx, proj_schema_idx, base, indirect_data,
                             projection_schema);
    } else if (col.is_nullable() && col.type_info()->physical_type() != BINARY) {
      CopyColumn<true, false>(block, t_schema_idx, proj_schema_idx, base, indirect_data,
                              projection_schema);
    } else if (!col.is_nullable() && col.type_info()->physical_type() == BINARY) {
      CopyColumn<false, true>(block, t_schema_idx, proj_schema_idx, base, indirect_data,
                              projection_schema);
    } else if (!col.is_nullable() && col.type_info()->physical_type() != BINARY) {
      CopyColumn<false, false>(block, t_schema_idx, proj_schema_idx, base, indirect_data,
                               projection_schema);
    } else {
      LOG(FATAL) << "cannot reach here";
    }
  }
  rowblock_pb->set_num_rows(rowblock_pb->num_rows() + num_rows);
}

} // namespace yb
