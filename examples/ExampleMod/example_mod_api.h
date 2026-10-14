// The ExampleMod.Greeter interface: a table ExampleMod provides to other mods
// with ProvideInterface. A mod that wants it asks GetInterface for
// EXAMPLE_MOD_GREETER_NAME with a minimum of EXAMPLE_MOD_GREETER_VERSION, then
// casts the table it gets to ExampleModGreeter. Copy this header into the mod
// that uses it.
#ifndef EXAMPLE_MOD_API_H
#define EXAMPLE_MOD_API_H

#define EXAMPLE_MOD_GREETER_NAME "ExampleMod.Greeter"
#define EXAMPLE_MOD_GREETER_VERSION 1

typedef struct ExampleModGreeter {
    // Logs a greeting for who. A NULL who is greeted as someone.
    void (*Greet)(const char* who);
} ExampleModGreeter;

#endif
