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
#include "abix/abix.hpp"

extern "C" void process(int value) { (void)value; }
extern "C" int process_int(int value) { return value * 2; }

SKL_ABIX_DEFINE_TABLE(SKL_ABIX_ENTRY("process", process), SKL_ABIX_ENTRY("process_int", process_int), )
