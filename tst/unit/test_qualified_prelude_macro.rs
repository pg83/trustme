//@ edition: 2015

mod nested {
    pub fn check() {
        std::panic!();
    }
}

fn main() {
    let _ = nested::check;
}
