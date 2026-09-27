# Info

This is a modified version of a dcomp stub implementation that compiles into a native Windows dll only, removing the wine version-dependent part. Only some functionalities will work.

Some additional stubs functionalities have been implemented using Claude Code. They resolve a couple program hangs and white popup issues mainly by providing stubs and preventing null pointers dereferencing.
  
Thanks to Zhiyi Zhang for providing original dcomp stub implementation from https://gitlab.winehq.org/zhiyi/wine/-/tree/bug-23698-react-native
