//@ proc-macro-aux-build: own_root_hygiene_program.rs
#![feature(decl_macro)]

extern crate own_root_hygiene_program;

own_root_hygiene_program::declare_program!();
