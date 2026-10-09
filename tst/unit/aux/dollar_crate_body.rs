pub trait Value {
    fn value() -> u32;
}

pub struct Seven;

impl Value for Seven {
    fn value() -> u32 {
        7
    }
}

pub fn helper() -> u32 {
    5
}

#[macro_export]
macro_rules! attributed {
    ($attr:path, $name:ident) => {
        #[$attr]
        pub fn $name() -> u32 {
            $crate::helper() + <$crate::Seven as $crate::Value>::value()
        }
    };
}
