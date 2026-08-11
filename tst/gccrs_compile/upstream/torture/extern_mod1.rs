// { dg-additional-options "-w" }

#[path = "extern_mod1/modules/mod.rs"]
mod modules;

fn main() {
    let twelve = modules::return_12();
}
