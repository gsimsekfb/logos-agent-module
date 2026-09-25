
### why did we give up using unique_ptr? 

The build was failing.

Context:  
https://github.com/gsimsekfb/logos-agent-module/commit/197395b9852523424d7d1575b4e44d9c75e446d6  

Details:   

The `logos-module-builder` codegen generates a **separate .cpp file** (`agent_module_module_impl.cpp`) that includes our header and instantiates `AgentModuleImpl`. That generated TU triggers implicit special member functions (destructor, move constructor) — and with `unique_ptr<OwnerChannel>`, all of them need the **complete type** of `OwnerChannel`, which is only defined in our `.cpp`.

Even declaring `~AgentModuleImpl()` in the header wasn't enough because the generated code also triggered the implicit **move constructor**, which also needs the complete type.

The options were:
1. **Raw pointer** (what we did) — simplest, no incomplete-type issue
2. Declare destructor + move ctor + move assignment in header, define all in `.cpp` — verbose boilerplate
3. Include `logos_lp_client.h` in the header — adds protocol dependency to every consumer

Raw pointer + `delete` in the destructor was the least invasive fix. If you prefer, I can switch to option 2 (full PIMPL with all special members declared).
