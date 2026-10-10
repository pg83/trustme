// diesel's `SqlType` for tuples up to 32 elements: a tuple's `IsNull` is
// `<T31::IsNull as OneIsNullable<<T30::IsNull as OneIsNullable<..>>::Out>>::Out`,
// a projection nested 31 deep. Upstream's normalizer folds a projection's
// arguments first and then projects it; the obligation it selects for keeps
// those arguments as they are, and a candidate is related to it structurally.
// Normalizing the arguments again for the goal - and again for every
// candidate it is related to - doubled the work at every level of nesting.
pub struct NotNull;
pub struct IsNullable;

pub trait OneIsNullable<Other> {
    type Out: OneIsNullable<IsNullable> + OneIsNullable<NotNull>;
}

impl OneIsNullable<NotNull> for NotNull {
    type Out = NotNull;
}

impl OneIsNullable<IsNullable> for NotNull {
    type Out = IsNullable;
}

impl OneIsNullable<NotNull> for IsNullable {
    type Out = IsNullable;
}

impl OneIsNullable<IsNullable> for IsNullable {
    type Out = IsNullable;
}

pub trait SqlType: 'static {
    type IsNull: OneIsNullable<IsNullable> + OneIsNullable<NotNull>;
}

impl SqlType for NotNull {
    type IsNull = NotNull;
}

pub struct Nullable;

impl SqlType for Nullable {
    type IsNull = IsNullable;
}

macro_rules! impl_sql_type {
    (
        @build
        start_ts = [$($ST: ident,)*],
        ts = [$T1: ident,],
        bounds = [$($bounds: tt)*],
        is_null = [$($is_null: tt)*],
    ) => {
        impl<$($ST,)*> SqlType for ($($ST,)*)
        where
            $($ST: SqlType,)*
            $($bounds)*
            $T1::IsNull: OneIsNullable<$($is_null)*>,
        {
            type IsNull = <$T1::IsNull as OneIsNullable<$($is_null)*>>::Out;
        }
    };
    (
        @build
        start_ts = [$($ST: ident,)*],
        ts = [$T1: ident, $($T: ident,)+],
        bounds = [$($bounds: tt)*],
        is_null = [$($is_null: tt)*],
    ) => {
        impl_sql_type! {
            @build
            start_ts = [$($ST,)*],
            ts = [$($T,)*],
            bounds = [$($bounds)* $T1::IsNull: OneIsNullable<$($is_null)*>,],
            is_null = [<$T1::IsNull as OneIsNullable<$($is_null)*>>::Out],
        }
    };
    ($T1: ident, $($T: ident,)+) => {
        impl_sql_type! {
            @build
            start_ts = [$T1, $($T,)*],
            ts = [$($T,)*],
            bounds = [],
            is_null = [$T1::IsNull],
        }
    };
}

impl_sql_type!(T0, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, T12, T13, T14, T15, T16, T17, T18, T19, T20, T21, T22, T23, T24, T25, T26, T27, T28, T29, T30, T31,);

fn is_nullable<T: SqlType>() -> bool
where
    T::IsNull: 'static,
{
    std::any::TypeId::of::<T::IsNull>() == std::any::TypeId::of::<IsNullable>()
}

fn main() {
    assert!(!is_nullable::<(NotNull, NotNull, NotNull, NotNull, NotNull, NotNull, NotNull, NotNull, NotNull, NotNull, NotNull, NotNull, NotNull, NotNull, NotNull, NotNull, NotNull, NotNull, NotNull, NotNull, NotNull, NotNull, NotNull, NotNull, NotNull, NotNull, NotNull, NotNull, NotNull, NotNull, NotNull, NotNull)>());
    assert!(is_nullable::<(NotNull, NotNull, NotNull, NotNull, NotNull, NotNull, NotNull, NotNull, NotNull, NotNull, NotNull, NotNull, NotNull, NotNull, NotNull, NotNull, NotNull, NotNull, NotNull, NotNull, NotNull, NotNull, NotNull, NotNull, NotNull, NotNull, NotNull, NotNull, NotNull, NotNull, NotNull, Nullable)>());
}
