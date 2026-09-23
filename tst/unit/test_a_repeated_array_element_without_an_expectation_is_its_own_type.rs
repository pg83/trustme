trait Handle {
    fn id(&self) -> usize;
}

struct Sender<T>(usize, Option<T>);

impl<T> Handle for Sender<T> {
    fn id(&self) -> usize {
        self.0
    }
}

impl<T> Sender<T> {
    fn send(&self, value: T) -> Option<T> {
        Some(value)
    }
}

fn unbind<'a, T>(x: &T) -> &'a T {
    unsafe { std::mem::transmute(x) }
}

fn main() {
    let tx = Sender::<i32>(7, None);
    let p = 1;
    let p = p.clone();
    let never: &dyn Handle = &Sender::<()>(0, None);
    let mut sel = [(never, 0, std::ptr::null()); 2];
    let var: &Sender<_> = unbind(&tx);
    sel[1] = (var, 1, var as *const Sender<_> as *const u8);
    assert_eq!(sel[1].0.id(), 7);
    assert_eq!(sel[0].0.id(), 0);
    assert_eq!(var.send(p), Some(1));
}
