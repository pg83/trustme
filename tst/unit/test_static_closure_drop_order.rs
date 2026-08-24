struct ClosureBox<'a> {
    closure: Box<dyn FnMut() + 'a>,
}

fn box_closure<'a>(closure: Box<dyn FnMut() + 'a>) -> ClosureBox<'a> {
    ClosureBox { closure }
}

fn call_static_closure(mut closure: ClosureBox<'static>) {
    (closure.closure)();
}

fn main() {
    call_static_closure(box_closure(Box::new(|| {})));
}
