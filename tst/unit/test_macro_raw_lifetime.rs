macro_rules! declare_lifetime {
    ($lifetime:lifetime) => {
        fn inner<$lifetime>() {}
    };
}

declare_lifetime!('r#struct);

fn main() {}
