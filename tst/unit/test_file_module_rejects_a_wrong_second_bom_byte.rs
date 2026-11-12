//@ input: aux/stdin_incomplete_bom_second.bin
//@ compile-fail: Incomplete BOM

#[path = "aux/stdin_incomplete_bom_second.bin"]
mod broken;

fn main() {}
