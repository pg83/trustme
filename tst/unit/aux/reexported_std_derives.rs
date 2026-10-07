pub mod exports {
    pub use std::{clone::Clone, default::Default, fmt::Debug};
}

#[macro_export]
macro_rules! with_std_derives {
    ($item:item) => {
        #[derive($crate::exports::Clone, $crate::exports::Default, $crate::exports::Debug)]
        $item
    };
}
