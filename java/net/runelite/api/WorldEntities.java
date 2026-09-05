// Shim of net.runelite.api.WorldEntities (BSD-2, RuneLite) -- the sub-world registry. No instances
// exist yet, so byIndex always reports "not found" the same way upstream does (null).
package net.runelite.api;

public class WorldEntities
{
	static final WorldEntities EMPTY = new WorldEntities();

	public WorldEntity byIndex(int index)
	{
		return null;
	}
}
