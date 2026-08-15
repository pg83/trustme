#![feature(type_alias_impl_trait)]

type ParentOpaque = impl Copy;

mod child_definer {
    #[define_opaque(super::ParentOpaque)]
    pub fn define_parent() -> super::ParentOpaque {
        1u32
    }
}

mod child_alias {
    pub type ChildOpaque = impl Copy;
}

#[define_opaque(child_alias::ChildOpaque)]
fn define_child() -> child_alias::ChildOpaque {
    2u32
}

fn main() {
    let _ = child_definer::define_parent();
    let _ = define_child();
}
