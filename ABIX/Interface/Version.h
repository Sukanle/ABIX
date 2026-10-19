/*
 * Copyright 2026 Sukanle(https://github.com/Sukanle)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef SKL_ABIX_INTERFACE_VERSION_H
#define SKL_ABIX_INTERFACE_VERSION_H

// Pack a semantic version into one unsigned integer:
//   major * 10'000'000 + minor * 100'000 + patch
// `major` and `minor` are reduced to two decimal digits, `patch` to five.
#define SKL_ABIX_VERSION_NUMBER(major, minor, patch) \
    ((((major) % 100U) * 10'000'000U) + (((minor) % 100U) * 100'000U) + ((patch) % 100'000U))

// Unpack a value produced by SKL_ABIX_VERSION_NUMBER into three unsigned int
// variables. The caller supplies the trailing semicolon:
//
//   SKL_ABIX_VERSION_EXTRACT(version, major, minor, patch);
//
#define SKL_ABIX_VERSION_EXTRACT(version, major, minor, patch) \
    unsigned int major = ((version) / 10'000'000U);            \
    unsigned int minor = (((version) / 100'000U) % 100U);      \
    unsigned int patch = ((version) % 100'000U)

#endif   // SKL_ABIX_INTERFACE_VERSION_H
