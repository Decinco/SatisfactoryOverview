#pragma once

#include "CoreMinimal.h"
#include "Buildables/FGBuildableFactory.h"

class FBuildableUnionFind
{
public:
	/** Non-asserting existence check — Find() asserts if never registered via MakeSet(). */
	bool Contains(AFGBuildable* Buildable) const { return Parent.Contains(Buildable); }

	/** Registers a buildable as its own singleton set, if not already known. */
	void MakeSet(AFGBuildable* Buildable)
	{
		if (!Parent.Contains(Buildable))
		{
			Parent.Add(Buildable, Buildable);
			Size.Add(Buildable, 1);
		}
	}

	/** Finds the representative ("root") of Buildable's cluster, with path compression. */
	AFGBuildable* Find(AFGBuildable* Buildable)
	{
		checkf(Parent.Contains(Buildable), TEXT("Find() called on a buildable that was never registered via MakeSet()"));

		AFGBuildable* Root = Buildable;
		while (Parent[Root] != Root)
		{
			Root = Parent[Root];
		}

		// Path compression: point everything along the walked path directly at the root.
		AFGBuildable* Current = Buildable;
		while (Parent[Current] != Root)
		{
			AFGBuildable* Next = Parent[Current];
			Parent[Current] = Root;
			Current = Next;
		}

		return Root;
	}

	/** Merges the two clusters containing A and B (no-op if already the same cluster). */
	void Union(AFGBuildable* A, AFGBuildable* B)
	{
		AFGBuildable* RootA = Find(A);
		AFGBuildable* RootB = Find(B);
		if (RootA == RootB)
		{
			return;
		}

		// Union by size
		if (Size[RootA] < Size[RootB])
		{
			Swap(RootA, RootB);
		}
		Parent[RootB] = RootA;
		Size[RootA] += Size[RootB];
	}

	/** Groups every registered buildable by its cluster root. Call once, after all Union()s are done. */
	TMap<AFGBuildable*, TArray<AFGBuildable*>> GetClusters()
	{
		TMap<AFGBuildable*, TArray<AFGBuildable*>> Clusters;
		for (auto& Pair : Parent)
		{
			AFGBuildable* Buildable = Pair.Key;
			AFGBuildable* Root = Find(Buildable); // re-Find to guarantee fully-compressed root
			Clusters.FindOrAdd(Root).Add(Buildable);
		}
		return Clusters;
	}

	int32 NumRegistered() const { return Parent.Num(); }

private:
	TMap<AFGBuildable*, AFGBuildable*> Parent;
	TMap<AFGBuildable*, int32> Size;
};
