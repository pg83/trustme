pub mod origin {
    pub struct Definition {
        pub value: u8,
    }
}

pub use origin::Definition as First;

pub mod relay {
    pub use super::First as Second;
}

pub use relay::Second as Exposed;
