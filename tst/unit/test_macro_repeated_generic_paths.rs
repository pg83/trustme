macro_rules! take_paths {
    ($($path:path),* $(,)*) => {};
}

fn main() {
    take_paths!(any, super, X<Y>::Z<'static, Item = U>);
}
