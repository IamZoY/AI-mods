// Shim of net.runelite.api.MenuEntry (BSD-2, RuneLite), cut to what the ported plugin creates and
// reads. In RuneLite a RUNELITE-type entry never reaches the server; here the entry is what gets
// injected into the client's menu (Phase D) and the onClick callback fires when the shim sees it
// clicked.
package net.runelite.api;

import java.util.function.Consumer;

import lombok.Getter;
import lombok.Setter;

@Getter
@Setter
public class MenuEntry
{
	private int identifier;
	private int type; // MenuAction id, stored as int like the game does
	private int opcode;
	private int param0;
	private int param1;
	private boolean forceLeftClick;
	private String option = "";
	private String target = "";
	private Consumer<MenuEntry> onClick;

	public int getItemId()
	{
		return identifier; // menu entries for items carry the item id in the identifier slot
	}

	public MenuEntry setType(MenuAction type)
	{
		this.type = type.getId();
		return this;
	}

	public MenuEntry setType(int type)
	{
		this.type = type;
		return this;
	}

	public MenuAction getType()
	{
		return MenuAction.of(type);
	}

	public MenuEntry onClick(Consumer<MenuEntry> callback)
	{
		this.onClick = callback;
		return this;
	}

	public MenuEntry setOption(String option)
	{
		this.option = option;
		return this;
	}

	public MenuEntry setTarget(String target)
	{
		this.target = target;
		return this;
	}

	public MenuEntry setIdentifier(int identifier)
	{
		this.identifier = identifier;
		return this;
	}

	public MenuEntry setParam0(int param0)
	{
		this.param0 = param0;
		return this;
	}

	public MenuEntry setParam1(int param1)
	{
		this.param1 = param1;
		return this;
	}

	public MenuEntry setOpcode(int opcode)
	{
		this.opcode = opcode;
		return this;
	}

	public MenuEntry setForceLeftClick(boolean forceLeftClick)
	{
		this.forceLeftClick = forceLeftClick;
		return this;
	}
}
