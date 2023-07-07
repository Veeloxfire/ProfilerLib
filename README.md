# TracerLib
Simple Tracing Library

Look at src/example.cpp to see how it is used

Now exports a custom trace to be viewed in the trace viewer (currently not publically available)

# Why things are as they are

This was developed as part of instrumentation for the Axle compiler
Any features/bugs exist because of the needs of that project

# Planned features/current limitations

- Trace viewer is being developed
- Currently only supports windows
- Currently has no protection against running over the buffer on the client side
- Currently *MUST* use another thread to do the writing (this might affect highly threaded applications in the future?)
- Currently no way to specify processes other than threads

