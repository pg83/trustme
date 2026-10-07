pub trait Derived: upcast_base::Base {
    fn extra(&self) -> u32;
}

pub struct Provider;

impl upcast_base::Base for Provider {
    fn id(&self) -> u32 {
        4
    }
}

impl Derived for Provider {
    fn extra(&self) -> u32 {
        5
    }
}
