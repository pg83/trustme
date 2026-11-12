//@ input: aux/stdin_incomplete_bom_third.bin
//@ compile-fail: Incomplete BOM

#[path = "aux/stdin_incomplete_bom_third.bin"]
mod broken;

fn main() {}
