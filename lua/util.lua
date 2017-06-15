local util = {}

-- set the magic bit for a color value
function util.truecolor(color)
    return bit32.bor(color, 0x1000000)
end

function util.bench(func, samples, batch_size)
    local times = {}
    for j=1, samples do
        local t1 = os.clock()

        for i=1, batch_size do
            res = func()
        end

        times[#times+1] = os.clock() - t1
    end
    return times
end

function util.stats(values, skip)
    local sum = values[skip + 1]
    local min = values[skip + 1]
    local max = values[skip + 1]
    for i=skip + 2, #values do
        local t = values[i]
        sum = sum + t
        if t < min then
            min = t
        end
        if max < t then
            max = t
        end
    end

    local count = (#values - skip)
    local mean = sum / count
    local delta = max - min

    sum = 0
    for i=skip + 2, #values do
        local vm = values[i] - mean
        sum = sum + (vm * vm)
    end
    local var = sum / count
    local sd = math.sqrt(var)

    return {
        count = count,
        min = min,
        max = max,
        mean = mean,
        delta = delta,
        var = var,
        sd = sd
    }
end

function util.report_bench_stats(name, stats)
    local pcterr = stats.delta / stats.mean * 100
    local bench_logger = logging.get("bench")
    bench_logger:info("Benchmark: " .. name)
    bench_logger:info(string.format("Average: %.5fs +/- %.5f (%.2f%%), Samples: %d",
        stats.mean, stats.delta, pcterr, stats.count))
    bench_logger:info(string.format("Variance: %.5f, Std Dev: %.5f", stats.var, stats.sd))
end

return util
