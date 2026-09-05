// The config proxy must answer every @ConfigItem with the interface's OWN declared default.
//
// This is the regression test for a bug that never failed a compile and never threw a stack trace:
// lookupDefault used findStatic for what are default (non-static) interface methods, every lookup
// failed, and every default silently degraded to a type default -- a @Range(min=1,max=30) item ended
// up with a default of 0, which JSlider then refused to construct, which killed the ENTIRE control
// panel. The test reads two ends of the range of shapes: an in-range int default, and an int default
// far outside any slider's comfort (currency threshold, 100000, which has no @Range at all).
package net.runelite.client.config;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

import org.junit.Test;
import shortestpath.ShortestPathConfig;

public class ConfigDefaultsTest
{
	@Test
	public void inRangeIntDefaultComesFromTheInterface()
	{
		assertEquals(5, (int) declaredDefault("calculationCutoff"));
	}

	@Test
	public void unRangedIntDefaultIsNotCapped()
	{
		assertEquals(100000, (int) declaredDefault("currencyThreshold"));
	}

	@Test
	public void declaringTheFullConfigSurvivesPanelConstruction()
	{
		// The crash was: declare() -> kewl Setting with an out-of-range default -> JSlider constructor
		// throws in Sidebar -> KewlKlient's catch eats it -> no control panel at all. So build the real
		// proxy against a real kewl Config and ask the real Sidebar code to build every control.
		kewl.config.Config kewlConfig = new kewl.config.Config();
		ConfigManager manager = new ConfigManager(null, kewlConfig);
		ShortestPathConfig config = manager.getConfig(ShortestPathConfig.class, kewlConfig);
		assertEquals(5, config.calculationCutoff());

		java.util.List<java.awt.Component> controls;
		try
		{
			java.lang.reflect.Method m = kewl.ui.Sidebar.class.getDeclaredMethod("controls",
				kewl.config.Config.class);
			m.setAccessible(true);
			controls = (java.util.List<java.awt.Component>) m.invoke(null, kewlConfig);
		}
		catch (ReflectiveOperationException e)
		{
			throw new AssertionError("could not build panel controls", e);
		}
		// The assertion is the construction above: it did not throw, and it produced one row per
		// declared setting (84 @ConfigItems, minus the 5 hidden ones the panel must not show).
		assertEquals(79, kewlConfig.all().size());
		assertTrue("panel produced no rows", controls.size() >= 79);
	}

	private static Object declaredDefault(String method)
	{
		for (java.lang.reflect.Method m : ShortestPathConfig.class.getMethods())
		{
			if (m.getName().equals(method))
			{
				return ConfigManager.lookupDefault(ShortestPathConfig.class, m);
			}
		}
		throw new AssertionError("no config method named " + method);
	}
}
