#pragma once

#include "CoreMinimal.h"
#include "Buildables/FGBuildableFactory.h"

/**
 * Union-Find (disjoint-set) over buildable actors, used during Phase 0's
 * scan and Phase 1's incremental merges to group buildables into clusters
 * (which later get classified into Factory / Nature / FuelStation / discard
 * per the design doc §4 step 6).
 *
 * IMPORTANT: this structure holds raw AFGBuildable* as keys. That's fine
 * because it is transient/in-memory only, built and consumed entirely
 * within a single scan pass -- it is never the thing that gets persisted.
 * Once clustering is done, the resulting groups get converted into whatever
 * safe/weak reference form the Factory subsystem actually stores (design
 * doc §7.2-7.3). Do NOT keep an instance of this around across ticks where
 * a buildable could be demolished in between -- Find()/Union() calls on a
 * dangling pointer would be undefined behavior. Placeholder header names
 * below (AFGBuildable) are assumed from the modding docs; verify against
 * the actual SDK header once available (see design doc §16).
 */
class FBuildableUnionFind
{
public:
	/** Registers a buildable as its own singleton set, if not already known. */
	void MakeSet(AFGBuildableFactory* Buildable)
	{
		if (!Parent.Contains(Buildable))
		{
			Parent.Add(Buildable, Buildable);
			Size.Add(Buildable, 1);
		}
	}

	/** Finds the representative ("root") of Buildable's cluster, with path compression. */
	AFGBuildableFactory* Find(AFGBuildableFactory* Buildable)
	{
		checkf(Parent.Contains(Buildable), TEXT("Find() called on a buildable that was never registered via MakeSet()"));

		AFGBuildableFactory* Root = Buildable;
		while (Parent[Root] != Root)
		{
			Root = Parent[Root];
		}

		// Path compression: point everything along the walked path directly at the root.
		AFGBuildableFactory* Current = Buildable;
		while (Parent[Current] != Root)
		{
			AFGBuildableFactory* Next = Parent[Current];
			Parent[Current] = Root;
			Current = Next;
		}

		return Root;
	}

	/** Merges the two clusters containing A and B (no-op if already the same cluster). */
	void Union(AFGBuildableFactory* A, AFGBuildableFactory* B)
	{
		AFGBuildableFactory* RootA = Find(A);
		AFGBuildableFactory* RootB = Find(B);
		if (RootA == RootB)
		{
			return;
		}

		// Union by size: attach the smaller set under the larger set's root.
		// This is the same "biggest wins" instinct as the Factory-merge rule
		// in design doc §5.1, but at the raw-clustering level, not the
		// classification level -- don't conflate the two. This just keeps
		// Find() shallow; it has nothing to do with which Factory name/ID
		// survives a merge, that's decided later during classification.
		if (Size[RootA] < Size[RootB])
		{
			Swap(RootA, RootB);
		}
		Parent[RootB] = RootA;
		Size[RootA] += Size[RootB];
	}

	/** Groups every registered buildable by its cluster root. Call once, after all Union()s are done. */
	TMap<AFGBuildableFactory*, TArray<AFGBuildableFactory*>> GetClusters()
	{
		TMap<AFGBuildableFactory*, TArray<AFGBuildableFactory*>> Clusters;
		for (auto& Pair : Parent)
		{
			AFGBuildableFactory* Buildable = Pair.Key;
			AFGBuildableFactory* Root = Find(Buildable); // re-Find to guarantee fully-compressed root
			Clusters.FindOrAdd(Root).Add(Buildable);
		}
		return Clusters;
	}

	int32 NumRegistered() const { return Parent.Num(); }

private:
	TMap<AFGBuildableFactory*, AFGBuildableFactory*> Parent;
	TMap<AFGBuildableFactory*, int32> Size;
};
