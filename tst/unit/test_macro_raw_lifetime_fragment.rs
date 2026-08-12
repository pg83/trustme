//@ edition: 2021

macro_rules! declare {
    ($lifetime:lifetime) => {
        fn generated<$lifetime>() {}
    };
}

declare!('r#struct);

fn main() {}
