
#include <linux/of.h>
#include <linux/of_address.h>

#include <linux/io.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/clkdev.h>

#include "clk-mtk.h"

#ifdef Bring_Up
#ifdef pr_debug
#undef pr_debug
#define pr_debug printk
#endif
#endif /* Bring_Up */

static DEFINE_SPINLOCK(clk_ops_lock);

spinlock_t *get_mtk_clk_lock(void)
{
	return &clk_ops_lock;
}


struct clk *mtk_clk_register_mux(
		const char *name,
		const char **parent_names,
		u8 num_parents,
		void __iomem *base_addr,
		u8 shift,
		u8 width,
		u8 gate_bit)
{
	struct clk *clk;
	struct clk_mux *mux;
	struct clk_gate *gate = NULL;
	struct clk_hw *gate_hw = NULL;
	const struct clk_ops *gate_ops = NULL;
	u32 mask = BIT(width) - 1;

	pr_debug("name: %s, num_parents: %d, gate_bit: %d\n",
		name, (int)num_parents, (int)gate_bit);

	mux = kzalloc(sizeof(struct clk_mux), GFP_KERNEL);
	if (!mux)
		return ERR_PTR(-ENOMEM);

	mux->reg = base_addr;
	mux->mask = mask;
	mux->shift = shift;
	mux->flags = 0;
	mux->lock = &clk_ops_lock;

	if (gate_bit <= MAX_MUX_GATE_BIT) {
		gate = kzalloc(sizeof(struct clk_gate), GFP_KERNEL);
		if (!gate) {
			kfree(mux);
			return ERR_PTR(-ENOMEM);
		}

		gate->reg = base_addr;
		gate->bit_idx = gate_bit;
		gate->flags = CLK_GATE_SET_TO_DISABLE;
		gate->lock = &clk_ops_lock;

		gate_hw = &gate->hw;
		gate_ops = &clk_gate_ops;
	}

	clk = clk_register_composite(NULL, name, parent_names, num_parents,
		&mux->hw, &clk_mux_ops,
		NULL, NULL,
		gate_hw, gate_ops,
		CLK_IGNORE_UNUSED);

	if (IS_ERR(clk)) {
		kfree(gate);
		kfree(mux);
	}

	return clk;
}
