macro_rules! define_exported {
    () => {
        #[macro_export]
        macro_rules! exported {
            () => {};
        }
    };
}

mod before_definition {
    use super::*;

    exported!();
}

mod definition {
    define_exported!();
}

fn main() {}
