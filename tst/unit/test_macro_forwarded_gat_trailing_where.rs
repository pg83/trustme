macro_rules! member { ($item:item) => { $item }; }
trait View { type Ref<'a> where Self: 'a; fn get(&self) -> Self::Ref<'_>; }
struct Host(u16);
impl View for Host {
    member!(type Ref<'a> = &'a u16 where Self: 'a;);
    member!(fn get(&self) -> Self::Ref<'_> { &self.0 });
}
fn main() { assert_eq!(*Host(257).get(),257); }
