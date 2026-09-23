trait Handle {
    fn id(&self) -> usize;
}

struct Sender<T>(usize, Option<T>);

impl<T> Handle for Sender<T> {
    fn id(&self) -> usize {
        self.0
    }
}

struct Chan<T>(usize, Option<T>);

impl<T> Chan<T> {
    fn tx(&self) -> Sender<T> {
        Sender(self.0, None)
    }
}

fn main() {
    let never: &dyn Handle = &Sender::<()>(0, None);
    let mut sel = [(never, 0); 2];
    let chan = Chan::<i32>(7, None);
    let tx = chan.tx();
    let var: &Sender<_> = &tx;
    sel[1] = (var, 1);
    assert_eq!(sel[1].0.id(), 7);
    assert_eq!(sel[0].0.id(), 0);
    let slice: &mut [(&dyn Handle, usize)] = &mut sel;
    slice[0] = (var, 2);
    assert_eq!(slice[0].0.id(), 7);
}
