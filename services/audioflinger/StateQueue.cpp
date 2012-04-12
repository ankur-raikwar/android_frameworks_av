/*
 * Copyright (C) 2012 The Android Open Source Project
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

#define LOG_TAG "StateQueue"
//#define LOG_NDEBUG 0

#include <time.h>
#include <cutils/atomic.h>
#include <utils/Log.h>
#include "StateQueue.h"

namespace android {

// Constructor and destructor

template<typename T> StateQueue<T>::StateQueue() :
    mNext(NULL), mAck(NULL), mCurrent(NULL),
    mMutating(&mStates[0]), mExpecting(NULL),
    mInMutation(false), mIsDirty(false), mIsInitialized(false)
{
}

template<typename T> StateQueue<T>::~StateQueue()
{
}

// Observer APIs

template<typename T> const T* StateQueue<T>::poll()
{
    const T *next = (const T *) android_atomic_acquire_load((volatile int32_t *) &mNext);
    if (next != mCurrent) {
        mAck = next;    // no additional barrier needed
        mCurrent = next;
    }
    return next;
}

// Mutator APIs

template<typename T> T* StateQueue<T>::begin()
{
    ALOG_ASSERT(!mInMutation, "begin() called when in a mutation");
    mInMutation = true;
    return mMutating;
}

template<typename T> void StateQueue<T>::end(bool didModify)
{
    ALOG_ASSERT(mInMutation, "end() called when not in a mutation");
    ALOG_ASSERT(mIsInitialized || didModify, "first end() must modify for initialization");
    if (didModify) {
        mIsDirty = true;
        mIsInitialized = true;
    }
    mInMutation = false;
}

template<typename T> bool StateQueue<T>::push(StateQueue<T>::block_t block)
{
#define PUSH_BLOCK_ACK_NS    3000000L   // 3 ms: time between checks for ack in push()
                                        //       FIXME should be configurable
    static const struct timespec req = {0, PUSH_BLOCK_ACK_NS};

    ALOG_ASSERT(!mInMutation, "push() called when in a mutation");

    if (mIsDirty) {

        // wait for prior push to be acknowledged
        if (mExpecting != NULL) {
            for (;;) {
                const T *ack = (const T *) mAck;    // no additional barrier needed
                if (ack == mExpecting) {
                    // unnecessary as we're about to rewrite
                    //mExpecting = NULL;
                    break;
                }
                if (block == BLOCK_NEVER) {
                    return false;
                }
                nanosleep(&req, NULL);
            }
        }

        // publish
        android_atomic_release_store((int32_t) mMutating, (volatile int32_t *) &mNext);
        mExpecting = mMutating;

        // copy with circular wraparound
        if (++mMutating >= &mStates[kN]) {
            mMutating = &mStates[0];
        }
        *mMutating = *mExpecting;
        mIsDirty = false;

    }

    // optionally wait for this push or a prior push to be acknowledged
    if (block == BLOCK_UNTIL_ACKED) {
        if (mExpecting != NULL) {
            for (;;) {
                const T *ack = (const T *) mAck;    // no additional barrier needed
                if (ack == mExpecting) {
                    mExpecting = NULL;
                    break;
                }
                nanosleep(&req, NULL);
            }
        }
    }

    return true;
}

}   // namespace android

// hack for gcc
#ifdef STATE_QUEUE_INSTANTIATIONS
#include STATE_QUEUE_INSTANTIATIONS
#endif