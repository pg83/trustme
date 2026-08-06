// Extracted from src/07_workarounds/03_send_approximation.md:23
use std::rc::Rc;
#[derive(Default)]
struct NotSend(Rc<()>);
async fn bar() {}
async fn foo() {
    NotSend::default();
    bar().await;
}

fn require_send(_: impl Send) {}

fn main() {
    require_send(foo());
}
