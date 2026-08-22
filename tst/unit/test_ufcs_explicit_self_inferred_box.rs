//@ edition: 2015

#[derive(Copy, Clone)]
struct BoxReceiver;

impl BoxReceiver {
    fn take(self: BoxReceiver) -> usize {
        1
    }

    fn borrow(self: &BoxReceiver) -> usize {
        2
    }

    fn take_again(self: Box<BoxReceiver>) -> usize {
        3
    }
}

#[derive(Copy, Clone)]
struct Value<T>(T);

impl<T> Value<T> {
    fn take(self: Value<T>) -> usize {
        1
    }

    fn borrow(self: &Value<T>) -> usize {
        2
    }

    fn take_again(self: Value<T>) -> usize {
        3
    }
}

fn main() {
    let first: Box<_> = Box::new(BoxReceiver);
    println!(
        "{} {} {}",
        first.take(),
        first.borrow(),
        first.take_again()
    );

    let second: Box<_> = Box::new(Value(1));
    println!(
        "{} {} {}",
        second.take(),
        second.borrow(),
        second.take_again()
    );
    let _: Box<Value<isize>> = second;
}
