/* Copyright (c) 2019, 2022, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 *
 */

#ifndef SHARE_OOPS_INSTANCESTACKCHUNKKLASS_INLINE_HPP
#define SHARE_OOPS_INSTANCESTACKCHUNKKLASS_INLINE_HPP

#include "oops/instanceStackChunkKlass.hpp"

#include "classfile/javaClasses.inline.hpp"
#include "code/codeBlob.inline.hpp"
#include "code/codeCache.inline.hpp"
#include "code/nativeInst.hpp"
#include "compiler/oopMap.hpp"
#include "gc/shared/barrierSetNMethod.hpp"
#include "gc/shared/collectedHeap.hpp"
#include "gc/shared/gc_globals.hpp"
#include "logging/log.hpp"
#include "memory/iterator.inline.hpp"
#include "oops/instanceKlass.inline.hpp"
#include "oops/klass.hpp"
#include "oops/oop.inline.hpp"
#include "oops/stackChunkOop.inline.hpp"
#include "runtime/frame.inline.hpp"
#include "runtime/globals.hpp"
#include "runtime/handles.inline.hpp"
#include "runtime/smallRegisterMap.inline.hpp"
#include "runtime/stackChunkFrameStream.inline.hpp"
#include "utilities/bitMap.inline.hpp"
#include "utilities/debug.hpp"
#include "utilities/globalDefinitions.hpp"
#include "utilities/macros.hpp"
#include CPU_HEADER_INLINE(instanceStackChunkKlass)

const int TwoWordAlignmentMask  = (1 << (LogBytesPerWord+1)) - 1;

inline size_t InstanceStackChunkKlass::instance_size(size_t stack_size_in_words) const {
  return align_object_size(size_helper() + stack_size_in_words + bitmap_size(stack_size_in_words));
}

inline size_t InstanceStackChunkKlass::bitmap_size(size_t stack_size_in_words) {
  if (!UseChunkBitmaps) {
    return 0;
  }
  size_t size_in_bits = bitmap_size_in_bits(stack_size_in_words);
  static const size_t mask = BitsPerWord - 1;
  int remainder = (size_in_bits & mask) != 0 ? 1 : 0;
  size_t res = (size_in_bits >> LogBitsPerWord) + remainder;
  assert (size_in_bits + bit_offset(stack_size_in_words) == (res << LogBitsPerWord), "");
  return res;
}

inline BitMap::idx_t InstanceStackChunkKlass::bit_offset(size_t stack_size_in_words) {
  static const size_t mask = BitsPerWord - 1;
  return (BitMap::idx_t)((BitsPerWord - (bitmap_size_in_bits(stack_size_in_words) & mask)) & mask);
}

template <InstanceStackChunkKlass::barrier_type barrier, chunk_frames frame_kind, typename RegisterMapT>
void InstanceStackChunkKlass::do_barriers(stackChunkOop chunk, const StackChunkFrameStream<frame_kind>& f,
                                          const RegisterMapT* map) {
  if (frame_kind == chunk_frames::MIXED) {
    // we could freeze deopted frames in slow mode.
    f.handle_deopted();
  }
  do_barriers0<barrier>(chunk, f, map);
}

template <typename T, class OopClosureType>
void InstanceStackChunkKlass::oop_oop_iterate(oop obj, OopClosureType* closure) {
  assert (obj->is_stackChunk(), "");
  stackChunkOop chunk = (stackChunkOop)obj;
  if (Devirtualizer::do_metadata(closure)) {
    Devirtualizer::do_klass(closure, this);
  }
  oop_oop_iterate_stack<OopClosureType>(chunk, closure);
  oop_oop_iterate_header<T>(chunk, closure);
}

template <typename T, class OopClosureType>
void InstanceStackChunkKlass::oop_oop_iterate_reverse(oop obj, OopClosureType* closure) {
  assert (obj->is_stackChunk(), "");
  assert(!Devirtualizer::do_metadata(closure), "Code to handle metadata is not implemented");
  stackChunkOop chunk = (stackChunkOop)obj;
  oop_oop_iterate_stack<OopClosureType>(chunk, closure);
  oop_oop_iterate_header<T>(chunk, closure);
}

template <typename T, class OopClosureType>
void InstanceStackChunkKlass::oop_oop_iterate_bounded(oop obj, OopClosureType* closure, MemRegion mr) {
  assert (obj->is_stackChunk(), "");
  stackChunkOop chunk = (stackChunkOop)obj;
  if (Devirtualizer::do_metadata(closure)) {
    if (mr.contains(obj)) {
      Devirtualizer::do_klass(closure, this);
    }
  }
  oop_oop_iterate_stack_bounded(chunk, closure, mr);
  oop_oop_iterate_header_bounded<T>(chunk, closure, mr);
}

template <typename T, class OopClosureType>
void InstanceStackChunkKlass::oop_oop_iterate_header(stackChunkOop chunk, OopClosureType* closure) {
  T* parent_addr = chunk->field_addr<T>(jdk_internal_vm_StackChunk::parent_offset());
  T* cont_addr = chunk->field_addr<T>(jdk_internal_vm_StackChunk::cont_offset());
  OrderAccess::storestore();
  Devirtualizer::do_oop(closure, parent_addr);
  OrderAccess::storestore();
  Devirtualizer::do_oop(closure, cont_addr); // must be last oop iterated
}

template <typename T, class OopClosureType>
void InstanceStackChunkKlass::oop_oop_iterate_header_bounded(stackChunkOop chunk, OopClosureType* closure, MemRegion mr) {
  T* parent_addr = chunk->field_addr<T>(jdk_internal_vm_StackChunk::parent_offset());
  T* cont_addr = chunk->field_addr<T>(jdk_internal_vm_StackChunk::cont_offset());
  if (mr.contains(parent_addr)) {
    OrderAccess::storestore();
    Devirtualizer::do_oop(closure, parent_addr);
  }
  if (mr.contains(cont_addr)) {
    OrderAccess::storestore();
    Devirtualizer::do_oop(closure, cont_addr); // must be last oop iterated
  }
}

template <class OopClosureType>
void InstanceStackChunkKlass::oop_oop_iterate_stack_bounded(stackChunkOop chunk, OopClosureType* closure, MemRegion mr) {
  if (LIKELY(chunk->has_bitmap())) {
    intptr_t* start = chunk->sp_address() - metadata_words();
    intptr_t* end = chunk->end_address();
    // mr.end() can actually be less than start. In that case, we only walk the metadata
    if ((intptr_t*)mr.start() > start) {
      start = (intptr_t*)mr.start();
    }
    if ((intptr_t*)mr.end() < end) {
      end = (intptr_t*)mr.end();
    }
    oop_oop_iterate_stack_helper(chunk, closure, start, end);
  } else {
    oop_oop_iterate_stack_slow(chunk, closure, mr);
  }
}

template <class OopClosureType>
void InstanceStackChunkKlass::oop_oop_iterate_stack(stackChunkOop chunk, OopClosureType* closure) {
  if (LIKELY(chunk->has_bitmap())) {
    oop_oop_iterate_stack_helper(chunk, closure, chunk->sp_address() - metadata_words(), chunk->end_address());
  } else {
    oop_oop_iterate_stack_slow(chunk, closure, chunk->range());
  }
}

template <typename OopT, typename OopClosureType>
class StackChunkOopIterateBitmapClosure {
  stackChunkOop _chunk;
  OopClosureType* const _closure;

public:
  StackChunkOopIterateBitmapClosure(stackChunkOop chunk, OopClosureType* closure) : _chunk(chunk), _closure(closure) {}

  bool do_bit(BitMap::idx_t index) {
    Devirtualizer::do_oop(_closure, _chunk->address_for_bit<OopT>(index));
    return true;
  }
};

template <class OopClosureType>
void InstanceStackChunkKlass::oop_oop_iterate_stack_helper(stackChunkOop chunk, OopClosureType* closure,
                                                           intptr_t* start, intptr_t* end) {
  if (Devirtualizer::do_metadata(closure)) {
    mark_methods(chunk, closure);
  }

  if (end > start) {
    if (UseCompressedOops) {
      StackChunkOopIterateBitmapClosure<narrowOop, OopClosureType> bitmap_closure(chunk, closure);
      chunk->bitmap().iterate(&bitmap_closure, chunk->bit_index_for((narrowOop*)start), chunk->bit_index_for((narrowOop*)end));
    } else {
      StackChunkOopIterateBitmapClosure<oop, OopClosureType> bitmap_closure(chunk, closure);
      chunk->bitmap().iterate(&bitmap_closure, chunk->bit_index_for((oop*)start), chunk->bit_index_for((oop*)end));
    }
  }
}

template <class StackChunkFrameClosureType>
inline void InstanceStackChunkKlass::iterate_stack(stackChunkOop obj, StackChunkFrameClosureType* closure) {
  obj->has_mixed_frames() ? iterate_stack<chunk_frames::MIXED>(obj, closure)
                          : iterate_stack<chunk_frames::COMPILED_ONLY>(obj, closure);
}

template <chunk_frames frame_kind, class StackChunkFrameClosureType>
inline void InstanceStackChunkKlass::iterate_stack(stackChunkOop obj, StackChunkFrameClosureType* closure) {
  const SmallRegisterMap* map = SmallRegisterMap::instance;
  assert (!map->in_cont(), "");

  StackChunkFrameStream<frame_kind> f(obj);
  bool should_continue = true;

  if (f.is_stub()) {
    RegisterMap full_map((JavaThread*)nullptr, true, false, true);
    full_map.set_include_argument_oops(false);

    f.next(&full_map);

    assert (!f.is_done(), "");
    assert (f.is_compiled(), "");

    should_continue = closure->template do_frame<frame_kind>((const StackChunkFrameStream<frame_kind>&)f, &full_map);
    f.next(map);
    f.handle_deopted(); // the stub caller might be deoptimized (as it's not at a call)
  }
  assert (!f.is_stub(), "");

  for(; should_continue && !f.is_done(); f.next(map)) {
    if (frame_kind == chunk_frames::MIXED) {
      // in slow mode we might freeze deoptimized frames
      f.handle_deopted();
    }
    should_continue = closure->template do_frame<frame_kind>((const StackChunkFrameStream<frame_kind>&)f, map);
  }
}

#endif // SHARE_OOPS_INSTANCESTACKCHUNKKLASS_INLINE_HPP
