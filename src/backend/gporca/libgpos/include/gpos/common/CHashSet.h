//---------------------------------------------------------------------------
//	Greenplum Database
//	Copyright (C) 2015 VMware, Inc. or its affiliates.
//
//	@filename:
//		CHashSet.h
//
//	@doc:
//		Hash set
//		* stores deep objects, i.e., pointers
//		* equality == on objects uses template function argument
//		* does not allow insertion of duplicates
//		* destroys objects based on client-side provided destroy functions
//
//	@owner:
//		solimm1
//
//	@test:
//
//
//---------------------------------------------------------------------------
#ifndef GPOS_CHashSet_H
#define GPOS_CHashSet_H

#include "gpos/base.h"
#include "gpos/common/CDynamicPtrArray.h"
#include "gpos/common/CRefCount.h"

namespace gpos
{
// fwd declaration
template <class T, ULONG (*HashFn)(const T *),
		  BOOL (*EqFn)(const T *, const T *), void (*CleanupFn)(T *)>
class CHashSetIter;

//---------------------------------------------------------------------------
//	@class:
//		CHashSet
//
//	@doc:
//		Hash set
//
//---------------------------------------------------------------------------
template <class T, ULONG (*HashFn)(const T *),
		  BOOL (*EqFn)(const T *, const T *), void (*CleanupFn)(T *)>
class CHashSet : public CRefCount
{
	// fwd declaration
	friend class CHashSetIter<T, HashFn, EqFn, CleanupFn>;

private:
	//---------------------------------------------------------------------------
	//	@class:
	//		CHashSetElem
	//
	//	@doc:
	//		Anchor for set element
	//
	//---------------------------------------------------------------------------
	class CHashSetElem
	{
	private:
		// pointer to object
		T *m_value;

		// does hash set element own object?
		BOOL m_owns_object;

	public:
		CHashSetElem(const CHashSetElem &) = delete;

		// ctor
		CHashSetElem(T *value, BOOL fOwn) : m_value(value), m_owns_object(fOwn)
		{
			GPOS_ASSERT(nullptr != value);
		}

		// dtor
		~CHashSetElem()
		{
			// in case of a temporary HashSet element for lookup we do NOT own the
			// objects, otherwise call destroy functions
			if (m_owns_object)
			{
				CleanupFn(m_value);
			}
		}

		// object accessor
		T *
		Value() const
		{
			return m_value;
		}

		// equality operator
		BOOL
		operator==(const CHashSetElem &hse) const
		{
			return EqFn(m_value, hse.m_value);
		}
	};	// class CHashSetElem

	// memory pool
	CMemoryPool *m_mp;

	// number of hash chains
	ULONG m_num_chains;

	// total number of entries
	ULONG m_size;

	// each hash chain is an array of hashset elements
	using HashSetElemArray = CDynamicPtrArray<CHashSetElem, CleanupDelete>;
	HashSetElemArray **m_chains;

	// array for elements
	// We use CleanupNULL because the elements are owned by the hash table
	using Elements = CDynamicPtrArray<T, CleanupNULL>;
	Elements *const m_elements;

	IntPtrArray *const m_filled_chains;

	// lookup appropriate hash chain in static table, may be NULL if
	// no elements have been inserted yet
	HashSetElemArray **
	GetChain(const T *value) const
	{
		GPOS_ASSERT(nullptr != m_chains);
		return &m_chains[HashFn(value) % m_num_chains];
	}

	// clear elements
	void
	Clear()
	{
		for (ULONG i = 0; i < m_filled_chains->Size(); i++)
		{
			// release each hash chain
			m_chains[*(*m_filled_chains)[i]]->Release();
		}
		m_size = 0;
		m_filled_chains->Clear();
	}

	// lookup an element by its key
	CHashSetElem *
	Lookup(const T *value) const
	{
		CHashSetElem hse(const_cast<T *>(value), false /*fOwn*/);
		CHashSetElem *found_hse = nullptr;
		HashSetElemArray **chain = GetChain(value);
		if (nullptr != *chain)
		{
			found_hse = (*chain)->Find(&hse);
			GPOS_ASSERT_IMP(nullptr != found_hse, *found_hse == hse);
		}

		return found_hse;
	}

public:
	CHashSet(const CHashSet &) = delete;

	// ctor
	CHashSet(CMemoryPool *mp, ULONG size = 127)
		: m_mp(mp),
		  m_num_chains(size),
		  m_size(0),
		  m_chains(GPOS_NEW_ARRAY(m_mp, HashSetElemArray *, m_num_chains)),
		  m_elements(GPOS_NEW(m_mp) Elements(m_mp)),
		  m_filled_chains(GPOS_NEW(mp) IntPtrArray(mp))
	{
		GPOS_ASSERT(size > 0);
		(void) clib::Memset(m_chains, 0,
							m_num_chains * sizeof(HashSetElemArray *));
	}

	// dtor
	~CHashSet() override
	{
		// release all hash chains
		Clear();

		GPOS_DELETE_ARRAY(m_chains);
		m_elements->Release();
		m_filled_chains->Release();
	}

	// insert an element if not present
	BOOL
	Insert(T *value)
	{
		if (Contains(value))
		{
			return false;
		}

		HashSetElemArray **chain = GetChain(value);
		if (nullptr == *chain)
		{
			*chain = GPOS_NEW(m_mp) HashSetElemArray(m_mp);
			INT chain_idx = HashFn(value) % m_num_chains;
			m_filled_chains->Append(GPOS_NEW(m_mp) INT(chain_idx));
		}

		CHashSetElem *elem = GPOS_NEW(m_mp) CHashSetElem(value, true /*fOwn*/);
		(*chain)->Append(elem);

		m_size++;
		m_elements->Append(value);

		return true;
	}

	// lookup element
	BOOL
	Contains(const T *value) const
	{
		CHashSetElem hse(const_cast<T *>(value), false /*fOwn*/);
		HashSetElemArray **chain = GetChain(value);
		if (nullptr != *chain)
		{
			CHashSetElem *found_hse = (*chain)->Find(&hse);

			return (nullptr != found_hse);
		}

		return false;
	}

	// return number of map entries
	ULONG
	Size() const
	{
		return m_size;
	}

	T *
	First()
	{
		if (m_elements->Size() == 0 || m_size == 0)
		{
			return nullptr;
		}

		return (*m_elements)[0];
	}

};	// class CHashSet

}  // namespace gpos

#endif	// !GPOS_CHashSet_H

// EOF
