//@ crate-type: lib
//@ compile-flags: --emit=metadata
#![feature(no_core, lang_items)]
#![no_core]

#[lang = "pointee_sized"]
trait PointeeSized {}
#[lang = "meta_sized"]
trait MetaSized: PointeeSized {}
#[lang = "sized"]
trait Sized: MetaSized {}

trait Project { type Output; }
impl Project for () { type Output = (); }
struct Head<T, U>(T, U);

trait EquationFirst<A, B> {}
impl<T, U> EquationFirst<T, U> for Head<T, U> {}
impl<T: Project, U> EquationFirst<&(T::Output, U), &T> for Head<&T, &U> {}

trait EquationFirstReversedImpls<A, B> {}
impl<T: Project, U> EquationFirstReversedImpls<&(T::Output, U), &T> for Head<&T, &U> {}
impl<T, U> EquationFirstReversedImpls<T, U> for Head<T, U> {}

trait AliasFirst<A, B> {}
impl<T, U> AliasFirst<U, T> for Head<T, U> {}
impl<T: Project, U> AliasFirst<&T, &(T::Output, U)> for Head<&T, &U> {}

trait AliasFirstReversedImpls<A, B> {}
impl<T: Project, U> AliasFirstReversedImpls<&T, &(T::Output, U)> for Head<&T, &U> {}
impl<T, U> AliasFirstReversedImpls<U, T> for Head<T, U> {}

trait MutualEquationFirst<A, B> {}
impl<T, U> MutualEquationFirst<T, U> for Head<T, U> {}
impl<T: Project, U: Project> MutualEquationFirst<&(T::Output, U), &(U::Output, T)> for Head<&T, &U> {}

trait MutualEquationFirstReversedImpls<A, B> {}
impl<T: Project, U: Project> MutualEquationFirstReversedImpls<&(T::Output, U), &(U::Output, T)> for Head<&T, &U> {}
impl<T, U> MutualEquationFirstReversedImpls<T, U> for Head<T, U> {}

trait MutualAliasFirst<A, B> {}
impl<T, U> MutualAliasFirst<U, T> for Head<T, U> {}
impl<T: Project, U: Project> MutualAliasFirst<&(U::Output, T), &(T::Output, U)> for Head<&T, &U> {}

trait MutualAliasFirstReversedImpls<A, B> {}
impl<T: Project, U: Project> MutualAliasFirstReversedImpls<&(U::Output, T), &(T::Output, U)> for Head<&T, &U> {}
impl<T, U> MutualAliasFirstReversedImpls<U, T> for Head<T, U> {}
