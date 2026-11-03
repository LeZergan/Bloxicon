cmake_minimum_required(VERSION 3.16)
add_library(rbx_luau_syntax STATIC
    Ast/src/Allocator.cpp Ast/src/Ast.cpp Ast/src/Confusables.cpp
    Ast/src/Cst.cpp Ast/src/Lexer.cpp Ast/src/Location.cpp
    Ast/src/Parser.cpp Ast/src/PrettyPrinter.cpp
    Common/src/StringUtils.cpp Common/src/TimeTrace.cpp
    ../LuauSyntax.cpp)
target_compile_features(rbx_luau_syntax PRIVATE cxx_std_17)
target_include_directories(rbx_luau_syntax PRIVATE Ast/include Common/include)
target_include_directories(rbx_luau_syntax PUBLIC ..)
if(NOT MSVC)
    target_compile_options(rbx_luau_syntax PRIVATE -O2 -ffunction-sections -fdata-sections)
endif()
