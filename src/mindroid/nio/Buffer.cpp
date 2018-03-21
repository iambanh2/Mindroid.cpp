/*
 * Copyright (C) 2017 E.S.R.Labs
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <mindroid/nio/Buffer.h>
#include <mindroid/nio/BufferOverflowException.h>
#include <mindroid/util/Assert.h>

namespace mindroid {

Buffer::Buffer(size_t capacity, bool readOnly) :
        mCapacity(capacity),
        mReadOnly(readOnly),
        mLimit(capacity) {
}

size_t Buffer::capacity() const {
    return mCapacity;
}

sp<Buffer> Buffer::clear() {
    mLimit = mCapacity;
    mPosition = 0;
    return this;
}

sp<Buffer> Buffer::flip() {
    mLimit = mPosition;
    mPosition = 0;
    return this;
}

bool Buffer::hasRemaining() const {
    return mPosition < mLimit;
}

bool Buffer::isReadOnly() const {
    return mReadOnly;
}

size_t Buffer::limit() const {
    return mLimit;
}

sp<Buffer> Buffer::limit(size_t newLimit) {
    Assert::assertFalse<BufferOverflowException>("Given limit exceeds capacity", newLimit > mCapacity);
    mLimit = newLimit;
    return this;
}

size_t Buffer::position() const {
    return mPosition;
}

sp<Buffer> Buffer::position(size_t newPos) {
    Assert::assertFalse<BufferOverflowException>("Cannot position outside of the buffer", newPos > mCapacity);
    mPosition = newPos;
    return this;
}

size_t Buffer::remaining() {
    return mPosition < mLimit ? mLimit - mPosition : 0;
}

sp<Buffer> Buffer::rewind() {
    mPosition = 0;
    return this;
}

sp<String> Buffer::toString() const {
    return String::format("Buffer: offset = %zu, position = %zu, limit = %zu, capacity = %zu", mOffset, mPosition, mLimit, mCapacity);
}

} /* namespace mindroid */
