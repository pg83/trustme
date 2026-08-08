#![feature(trait_alias)]

trait Principal {}
trait Object = Principal + Send;
trait Generic<T> {}
trait GenericObject<T = u8> = Generic<T> + Sync;
trait ConstGeneric<const N: usize> {}
trait ConstObject<const N: usize = 4> = ConstGeneric<N> + Send;
trait Lifetime<'a> {}
trait LifetimeObject<'a> = Lifetime<'a>;
trait Associated { type Item; }
trait AssociatedObject = Associated;
trait Higher<'a> {}
trait HigherObject<'a> = Higher<'a>;
trait TwoLifetimes<'a, 'b> {}
trait InnerHigher<'a> = for<'b> TwoLifetimes<'a, 'b>;
trait OuterHigher<'a> = InnerHigher<'a>;

fn higher_ranked(_: &dyn for<'a> HigherObject<'a>) {}
fn nested_higher_ranked(_: &dyn for<'a> OuterHigher<'a>) {}

fn main() {
    let _: Box<dyn Object>;
    let _: Box<dyn GenericObject>;
    let _: Box<dyn ConstObject>;
    let _: Box<dyn LifetimeObject<'static>>;
    let _: Box<dyn AssociatedObject<Item = u8>>;
}
