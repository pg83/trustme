//@ edition: 2021

struct Owner<const N: usize>;

impl<const N: usize> Owner<N> {
    fn local_name() -> &'static str {
        struct Local;
        std::any::type_name::<Local>()
    }
}

fn main() {
    assert_eq!(
        Owner::<7>::local_name(),
        concat!(module_path!(), "::Owner<_>::local_name::Local")
    );
}
