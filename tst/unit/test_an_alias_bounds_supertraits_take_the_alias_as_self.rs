// icu_calendar compares `Date`s by `self.inner.eq(&other.inner)`, where
// `inner: <C as Calendar>::DateInner` and `type DateInner: PartialEq + Eq + ..`.
// `Eq`'s supertrait `PartialEq<Self>` has for `Self` the bounded type, the
// projection itself; the method lookup elaborated it with a placeholder and then
// substituted the placeholder as the `Self` of `Calendar`, looking for
// `PartialEq<C>`. rustc elaborates an alias bound's supertraits with the alias
// as their self type.
trait Calendar {
    type DateInner: PartialEq + Eq + Clone + std::fmt::Debug;
}

trait AsCalendar {
    type Calendar: Calendar;
}

struct Date<A: AsCalendar> {
    inner: <A::Calendar as Calendar>::DateInner,
}

impl<C, A, B> PartialEq<Date<B>> for Date<A>
where
    C: Calendar,
    A: AsCalendar<Calendar = C>,
    B: AsCalendar<Calendar = C>,
{
    fn eq(&self, other: &Date<B>) -> bool {
        self.inner.eq(&other.inner)
    }
}

struct Iso;

impl Calendar for Iso {
    type DateInner = u32;
}

impl<C: Calendar> AsCalendar for C {
    type Calendar = C;
}

fn main() {
    assert!(Date::<Iso> { inner: 3 } == Date::<Iso> { inner: 3 });
    assert!(Date::<Iso> { inner: 3 } != Date::<Iso> { inner: 4 });
}
