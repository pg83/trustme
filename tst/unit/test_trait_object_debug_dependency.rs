//@ edition: 2015

fn main() {
    fn make() -> Box<dyn std::any::Any + 'static> {
        Box::new(1)
    }

    println!("{:?}", make());
}
