// Shim of net.runelite.client.util.Text (BSD-2, RuneLite) -- the used surface only: the plugin strips
// colour/col tags out of widget text and counts characters for its fairy-ring parsing.
package net.runelite.client.util;

import java.util.Collection;
import java.util.stream.Collectors;

public class Text
{
	private static final int[] FOOTER = {'<', '>'};

	public static String removeTags(String str)
	{
		if (str == null)
		{
			return "";
		}
		StringBuilder out = new StringBuilder(str.length());
		for (int i = 0; i < str.length(); i++)
		{
			char c = str.charAt(i);
			if (c == '<')
			{
				int close = str.indexOf('>', i);
				if (close == -1)
				{
					return out.toString();
				}
				i = close;
				continue;
			}
			out.append(c);
		}
		return out.toString();
	}

	public static String standardize(String str)
	{
		if (str == null)
		{
			return "";
		}
		return removeTags(str).replaceAll("[  ]", " ").trim();
	}

	public static boolean contains(Collection<String> haystack, String needle)
	{
		return haystack.stream()
			.map(Text::standardize)
			.collect(Collectors.toList())
			.contains(standardize(needle));
	}

	public static int length(String str)
	{
		return removeTags(str).length();
	}
}
