use std::any::Any;

struct Sender<T>(Option<T>);

struct Chan<T>(Option<T>);

impl<T> Chan<T> {
    fn tx(&self) -> Sender<T> {
        Sender(None)
    }
}

struct Op;

impl Op {
    fn send<T>(self, _s: &Sender<T>, msg: T) -> Option<T> {
        Some(msg)
    }
}

fn unbind<'a, T>(x: &T) -> &'a T {
    unsafe { std::mem::transmute(x) }
}

struct Mt;

fn main() {
    let c = Chan::<Box<dyn Any>>(None);
    let t = c.tx();
    let var: &Sender<_> = {
        let s: &Sender<_> = &t;
        unbind(s)
    };
    let sent = Op.send(var, Box::new(Mt));
    assert!(sent.unwrap().is::<Mt>());
    let other = unbind(&t);
    let again = Op.send(other, Box::new(7u8));
    assert!(again.unwrap().is::<u8>());
}
