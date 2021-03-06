/*
 *	Device handling code
 *	Linux ethernet bridge
 *
 *	Authors:
 *	Lennert Buytenhek		<buytenh@gnu.org>
 *
 *	$Id: //BBN_Linux/Branch/Branch_for_SDK_Release_MultiChip_20111013/tclinux_phoenix/linux/net/bridge/br_device.c#1 $
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>

#include <asm/uaccess.h>
#include "br_private.h"

#ifdef CONFIG_IGMP_SNOOPING
MODULE_LICENSE("GPL");
int (*br_mc_deliver_hook)(struct net_bridge *br, struct sk_buff *skb, int clone);
EXPORT_SYMBOL_GPL(br_mc_deliver_hook);
#endif

#ifdef CONFIG_MLD_SNOOPING
int (*br_mldsnooping_deliver_hook)(struct sk_buff *skb, struct net_bridge *br, unsigned char *dest,int clone);
EXPORT_SYMBOL(br_mldsnooping_deliver_hook);
#endif

static struct net_device_stats *br_dev_get_stats(struct net_device *dev)
{
	struct net_bridge *br = netdev_priv(dev);
	return &br->statistics;
}

/* net device transmit always called with no BH (preempt_disabled) */
__IMEM int br_dev_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct net_bridge *br = netdev_priv(dev);
	const unsigned char *dest = skb->data;
	struct net_bridge_fdb_entry *dst;
#if defined(CONFIG_MLD_SNOOPING) || defined(CONFIG_IGMP_SNOOPING)
	int ret = 0;
#endif
#ifdef CONFIG_MLD_SNOOPING
	typeof(br_mldsnooping_deliver_hook) br_mldsnooping_deliver;
#endif	
#ifdef CONFIG_IGMP_SNOOPING
	typeof(br_mc_deliver_hook) br_mc_deliver_igmpsnoop;
#endif
	br->statistics.tx_packets++;
	br->statistics.tx_bytes += skb->len;

	skb_reset_mac_header(skb);
	skb_pull(skb, ETH_HLEN);

	if (dest[0] & 1)
#if defined(CONFIG_MLD_SNOOPING) || defined(CONFIG_IGMP_SNOOPING)
	{
		switch(ntohs(skb->protocol)){
			#ifdef CONFIG_IGMP_SNOOPING
			case ETH_P_IP:	/*IGMP Snooping*/	
			br_mc_deliver_igmpsnoop = rcu_dereference(br_mc_deliver_hook);
			if(br_mc_deliver_igmpsnoop)
				ret = br_mc_deliver_igmpsnoop(br, skb, 0);		
			break;
			#endif
			#ifdef CONFIG_MLD_SNOOPING
			case ETH_P_IPV6: /*MLD Snooping*/
				br_mldsnooping_deliver = rcu_dereference(br_mldsnooping_deliver_hook);
				if(br_mldsnooping_deliver)
					ret = br_mldsnooping_deliver(skb, br, dest,0);
				break;
			#endif
			default:
				ret = 0;
				break;
		}
		if(!ret){
			br_flood_deliver(br, skb, 0);
		}
	}
#else
		br_flood_deliver(br, skb, 0);
#endif
	else if ((dst = __br_fdb_get(br, dest)) != NULL)
		br_deliver(dst->dst, skb);
	else
		br_flood_deliver(br, skb, 0);

	return 0;
}

static int br_dev_open(struct net_device *dev)
{
	struct net_bridge *br = netdev_priv(dev);

	br_features_recompute(br);
	netif_start_queue(dev);
	br_stp_enable_bridge(br);

	return 0;
}

static void br_dev_set_multicast_list(struct net_device *dev)
{
}

static int br_dev_stop(struct net_device *dev)
{
	br_stp_disable_bridge(netdev_priv(dev));

	netif_stop_queue(dev);

	return 0;
}

static int br_change_mtu(struct net_device *dev, int new_mtu)
{
	if (new_mtu < 68 || new_mtu > br_min_mtu(netdev_priv(dev)))
		return -EINVAL;

	dev->mtu = new_mtu;
	return 0;
}

/* Allow setting mac address to any valid ethernet address. */
static int br_set_mac_address(struct net_device *dev, void *p)
{
	struct net_bridge *br = netdev_priv(dev);
	struct sockaddr *addr = p;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EINVAL;

	spin_lock_bh(&br->lock);
	memcpy(dev->dev_addr, addr->sa_data, ETH_ALEN);
	br_stp_change_bridge_id(br, addr->sa_data);
	spin_unlock_bh(&br->lock);

	return 0;
}

static void br_getinfo(struct net_device *dev, struct ethtool_drvinfo *info)
{
	strcpy(info->driver, "bridge");
	strcpy(info->version, BR_VERSION);
	strcpy(info->fw_version, "N/A");
	strcpy(info->bus_info, "N/A");
}

static int br_set_sg(struct net_device *dev, u32 data)
{
	struct net_bridge *br = netdev_priv(dev);

	if (data)
		br->feature_mask |= NETIF_F_SG;
	else
		br->feature_mask &= ~NETIF_F_SG;

	br_features_recompute(br);
	return 0;
}

static int br_set_tso(struct net_device *dev, u32 data)
{
	struct net_bridge *br = netdev_priv(dev);

	if (data)
		br->feature_mask |= NETIF_F_TSO;
	else
		br->feature_mask &= ~NETIF_F_TSO;

	br_features_recompute(br);
	return 0;
}

static int br_set_tx_csum(struct net_device *dev, u32 data)
{
	struct net_bridge *br = netdev_priv(dev);

	if (data)
		br->feature_mask |= NETIF_F_NO_CSUM;
	else
		br->feature_mask &= ~NETIF_F_ALL_CSUM;

	br_features_recompute(br);
	return 0;
}

static struct ethtool_ops br_ethtool_ops = {
	.get_drvinfo = br_getinfo,
	.get_link = ethtool_op_get_link,
	.get_sg = ethtool_op_get_sg,
	.set_sg = br_set_sg,
	.get_tx_csum = ethtool_op_get_tx_csum,
	.set_tx_csum = br_set_tx_csum,
	.get_tso = ethtool_op_get_tso,
	.set_tso = br_set_tso,
};

void br_dev_setup(struct net_device *dev)
{
	memset(dev->dev_addr, 0, ETH_ALEN);

	ether_setup(dev);

	dev->do_ioctl = br_dev_ioctl;
	dev->get_stats = br_dev_get_stats;
	dev->hard_start_xmit = br_dev_xmit;
	dev->open = br_dev_open;
	dev->set_multicast_list = br_dev_set_multicast_list;
	dev->change_mtu = br_change_mtu;
	dev->destructor = free_netdev;
	SET_MODULE_OWNER(dev);
	SET_ETHTOOL_OPS(dev, &br_ethtool_ops);
	dev->stop = br_dev_stop;
	dev->tx_queue_len = 0;
	dev->set_mac_address = br_set_mac_address;
	dev->priv_flags = IFF_EBRIDGE;

	dev->features = NETIF_F_SG | NETIF_F_FRAGLIST | NETIF_F_HIGHDMA |
			NETIF_F_GSO_SOFTWARE | NETIF_F_NO_CSUM |
			NETIF_F_GSO_ROBUST | NETIF_F_LLTX;
}
