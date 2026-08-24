//@ test-harness

pub mod util {
    pub mod macro_util {
        pub const fn hash_name(_: &str) -> u64 {
            42
        }
    }
}

#[macro_export]
macro_rules! ident_id {
    ($field:ident) => {
        $crate::util::macro_util::hash_name(stringify!($field))
    };
}

struct HasField<const ID: u64>;

#[test]
fn dollar_crate_stays_local() {
    let _: HasField<{ crate::ident_id!(value) }> = HasField;
}
