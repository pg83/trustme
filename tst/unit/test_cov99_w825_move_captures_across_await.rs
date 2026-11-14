fn main() {
    let first = String::from("first");
    let second = String::from("second");
    let future = async move {
        std::future::ready(()).await;
        (first, second)
    };
    drop(future);
}
