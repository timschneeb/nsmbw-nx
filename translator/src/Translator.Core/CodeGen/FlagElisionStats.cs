using System;
using System.Collections.Concurrent;
using System.Linq;

namespace Translator.Core.CodeGen;

/// <summary>
/// Env-gated counters for <see cref="CxxLinearCodeGenerator.PlanFlagElision"/>. Set
/// <c>NSMBW_FLAGSTATS=1</c> to get a histogram of why compare/branch pairs were not fused, printed
/// when the translator process exits. Costs nothing when disabled.
/// </summary>
internal static class FlagElisionStats
{
    public static readonly bool Enabled =
        Environment.GetEnvironmentVariable("NSMBW_FLAGSTATS") is "1" or "on";

    private static readonly ConcurrentDictionary<string, long> Counters = new();

    static FlagElisionStats()
    {
        if (Enabled)
        {
            AppDomain.CurrentDomain.ProcessExit += (_, _) => Dump();
        }
    }

    public static void Count(string key)
    {
        if (!Enabled)
        {
            return;
        }

        Counters.AddOrUpdate(key, 1, static (_, value) => value + 1);
    }

    public static void Add(string key, long value)
    {
        if (!Enabled)
        {
            return;
        }

        Counters.AddOrUpdate(key, value, (_, current) => current + value);
    }

    private static void Dump()
    {
        var ordered = Counters.OrderByDescending(pair => pair.Value).ToList();
        var totalCompares = Counters.TryGetValue("cmp.total", out var compares) ? compares : 0;
        var fused = Counters.TryGetValue("fused", out var fusedCount) ? fusedCount : 0;
        Console.Error.WriteLine("[flagstats] flag elision histogram");
        foreach (var (key, value) in ordered)
        {
            var share = totalCompares > 0 && key.StartsWith("reject.", StringComparison.Ordinal)
                ? $"  ({100.0 * value / totalCompares:F1}% of compares)"
                : string.Empty;
            Console.Error.WriteLine($"[flagstats]   {value,10}  {key}{share}");
        }

        if (totalCompares > 0)
        {
            Console.Error.WriteLine(
                $"[flagstats]   fused {fused} of {totalCompares} compares ({100.0 * fused / totalCompares:F1}%)");
        }
    }
}
