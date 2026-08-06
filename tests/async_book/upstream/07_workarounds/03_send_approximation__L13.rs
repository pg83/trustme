// Extracted from src/07_workarounds/03_send_approximation.md:13
#![allow(unused)]
fn main() {
    use std::rc::Rc;
    
    #[derive(Default)]
    struct NotSend(Rc<()>);
}
