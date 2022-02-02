# TracerLib
Simple Tracing Library

Look at src/example.cpp to see how it is used

This library produces a google tracing json file from your code

You can then use this with whatever trace viewer you prefer

# Why things are as they are

This was developed as part of instrumentation for the Axle compiler
Any features/bugs exist because of the needs of that project

# Planned features/current limitations

- Currently only supports windows
- Currently has no protection against running over the buffer on the client side
- Currently *MUST* use another thread to do the writing (this might affect highly threaded applications in the future?)
- Currently only supports one thread and one process id

