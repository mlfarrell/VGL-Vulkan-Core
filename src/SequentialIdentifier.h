/*********************************************************************
Copyright 2018 VERTO STUDIO LLC.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
***************************************************************************/

#pragma once

#include <atomic>

namespace vgl
{
  namespace core
  {
    ///Objects derived from this will have unique-to-their-own-type IDs that are gauranteed to not repeat 
    ///during the program lifetime.  This gets around dodgy pointer-as-hash-key issue where pointers could possibly repeat.  
    class SequentialIdentifier
    {
    public:
      static std::atomic_uint32_t globalId;

      SequentialIdentifier() { identifier = globalId++; }
      inline uint32_t getId() { return identifier; }

    private:
      uint32_t identifier;
    };
  }
}
