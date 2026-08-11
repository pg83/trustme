use issue_4402_foo::Bar;

pub mod issue_4402_foo {
    pub struct Bar;
}

fn main() {
    // use '_a' to silence the unused variable warning
    let _a = Bar;
}
