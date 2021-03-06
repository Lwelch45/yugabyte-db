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
#ifndef YB_CFILE_BLOOMFILE_H
#define YB_CFILE_BLOOMFILE_H

#include <boost/ptr_container/ptr_vector.hpp>
#include <string>
#include <vector>

#include "yb/cfile/cfile_reader.h"
#include "yb/cfile/cfile_writer.h"
#include "yb/gutil/macros.h"
#include "yb/util/bloom_filter.h"
#include "yb/util/faststring.h"
#include "yb/util/mem_tracker.h"
#include "yb/util/once.h"
#include "yb/util/status.h"

namespace yb {
namespace cfile {

class BloomFileWriter {
 public:
  BloomFileWriter(gscoped_ptr<fs::WritableBlock> block,
                  const BloomFilterSizing &sizing);

  CHECKED_STATUS Start();
  CHECKED_STATUS AppendKeys(const Slice *keys, size_t n_keys);

  // Close the bloom's CFile, closing the underlying writable block.
  CHECKED_STATUS Finish();

  // Close the bloom's CFile, releasing the underlying block to 'closer'.
  CHECKED_STATUS FinishAndReleaseBlock(fs::ScopedWritableBlockCloser* closer);

  // Estimate the amount of data already written to this file.
  size_t written_size() const;

 private:
  DISALLOW_COPY_AND_ASSIGN(BloomFileWriter);

  CHECKED_STATUS FinishCurrentBloomBlock();

  gscoped_ptr<cfile::CFileWriter> writer_;

  BloomFilterBuilder bloom_builder_;

  // first key inserted in the current block.
  faststring first_key_;
};

// Reader for a bloom file.
// NB: this is not currently thread-safe.
// When making it thread-safe, should make sure that the threads
// share a single CFileReader, or else the cache keys won't end up
// shared!
class BloomFileReader {
 public:

  // Fully open a bloom file using a previously opened block.
  //
  // After this call, the bloom reader is safe for use.
  static CHECKED_STATUS Open(gscoped_ptr<fs::ReadableBlock> block,
                     const ReaderOptions& options,
                     gscoped_ptr<BloomFileReader> *reader);

  // Lazily opens a bloom file using a previously opened block. A lazy open
  // does not incur additional I/O, nor does it validate the contents of
  // the bloom file.
  //
  // Init() must be called before using CheckKeyPresent().
  static CHECKED_STATUS OpenNoInit(gscoped_ptr<fs::ReadableBlock> block,
                           const ReaderOptions& options,
                           gscoped_ptr<BloomFileReader> *reader);

  // Fully opens a previously lazily opened bloom file, parsing and
  // validating its contents.
  //
  // May be called multiple times; subsequent calls will no-op.
  CHECKED_STATUS Init();

  // Check if the given key may be present in the file.
  //
  // Sets *maybe_present to false if the key is definitely not
  // present, otherwise sets it to true to indicate maybe present.
  CHECKED_STATUS CheckKeyPresent(const BloomKeyProbe &probe,
                         bool *maybe_present);

 private:
  DISALLOW_COPY_AND_ASSIGN(BloomFileReader);

  BloomFileReader(gscoped_ptr<CFileReader> reader, const ReaderOptions& options);

  // Parse the header present in the given block.
  //
  // Returns the parsed header inside *hdr, and returns
  // a Slice to the true bloom filter data inside
  // *bloom_data.
  CHECKED_STATUS ParseBlockHeader(const Slice &block,
                          BloomBlockHeaderPB *hdr,
                          Slice *bloom_data) const;

  // Callback used in 'init_once_' to initialize this bloom file.
  CHECKED_STATUS InitOnce();

  // Returns the memory usage of this object including the object itself but
  // excluding the CFileReader, which is tracked independently.
  size_t memory_footprint_excluding_reader() const;

  gscoped_ptr<CFileReader> reader_;

  // TODO: temporary workaround for the fact that
  // the index tree iterator is a member of the Reader object.
  // We need a big per-thread object which gets passed around so as
  // to avoid this... Instead we'll use a per-CPU iterator as a
  // lame hack.
  boost::ptr_vector<cfile::IndexTreeIterator> index_iters_;
  gscoped_ptr<padded_spinlock[]> iter_locks_;

  YBOnceDynamic init_once_;

  ScopedTrackedConsumption mem_consumption_;
};

} // namespace cfile
} // namespace yb

#endif
