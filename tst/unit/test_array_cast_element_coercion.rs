fn callback() {}

fn main() {
    let callbacks = [callback] as [fn(); 1];
    callbacks[0]();
}
