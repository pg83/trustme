macro_rules! expression {
    ($value:expr) => {
        $value
    };
}

fn main() {
    let _ = expression!(async { 1 });
    let _ = expression!(async move { 2 });
}
