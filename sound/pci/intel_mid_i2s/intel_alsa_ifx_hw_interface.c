/*
 *   intel_alsa_ifx_hw_interface.c - Intel Sound card driver for IFX
 *
 *  Copyright (C) 2010 Intel Corp
 *  Authors:	Selma Bensaid <selma.bensaid@intel.com>
 *  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */
#include <sound/pcm.h>

#include "intel_alsa_ifx_hw_interface.h"
#include "intel_alsa_ifx_hw_private.h"

/*****************************************/
/* Global Variables                      */
/*****************************************/
static struct intel_alsa_stream_status s_streamifx_status;

void intel_alsa_reset_ifx_status(void)
{
	s_streamifx_status.ssp_handle = NULL;
	s_streamifx_status.stream_info = 0;
	spin_lock_init(&s_streamifx_status.lock);
}

/*
 * intel_alsa_ifx_open - This function opens the requested stream
 * The Intel I2S driver is opened only if all streams has been closed
 *
 * Input parameters
 *		str_info : pointer to stream structure
 * Output parameters
 *		ret_val : status
 */
static int intel_alsa_ifx_open(struct intel_alsa_ifx_stream_info *str_info)
{
	int device_id, status;
	bool open_ifx = false;

	WARN(!str_info, "ALSA_IFX: NULL str_info\n");
	if (!str_info)
		return -EINVAL;

	device_id = str_info->device_id;

	str_info->dma_slot.period_req_index = 0;
	str_info->dma_slot.period_cb_index = 0;

	/*
	 * One Open Callback is called by sub-stream:
	 *  1 for BT Playback0
	 *  1 for BT Capture
	 *  1 for FM Capture
	 *  The FM and BT are exclusive, the 2 devices cannot be opened in
	 *  parallel
	 */

	pr_debug("ALSA_IFX: call intel_mid_i2s_open for Device ID = %d\n",
			device_id);

	/*
	 * Algorithm that detects conflicting call of open /close
	 * The Open Call back is called for each device substream
	 * BT Capture, BT Playback and FM Capture
	 * However the Intel MID SSP provide a unique interface
	 * to open/configure and to close the SSP
	 * The aim of this function is to properly handle the concurrent
	 * Open/Close Callbacks
	 * The s_streamifx_status.stream_info is a bit field that indicated
	 * the stream status open or close
	 * BT Capture substream index = 0
	 * BT Play substream index = 1
	 * FM Capture substream index = 2
	 */
	spin_lock(&s_streamifx_status.lock);
	if (s_streamifx_status.stream_info == 0) {
		open_ifx = true;
	} else if ((((str_info->stream_index == 0)) && (test_bit(1, &s_streamifx_status.stream_info))) ||
			((str_info->stream_index == 1) && (test_bit(0, &s_streamifx_status.stream_info)))) {
		/*
		 * do nothing be cause already Open by sibling substream
		 */
		pr_debug("ALSA IFX: Open DO NOTHING\n");

	} else {
		spin_unlock(&s_streamifx_status.lock);
		WARN(1, "ALSA IFX: Open unsupported Config\n");
		return -EBUSY;
	}
	set_bit(str_info->stream_index, &s_streamifx_status.stream_info);
	spin_unlock(&s_streamifx_status.lock);

	/*
	 * the Open IFX is performed out of lock
	 */
	if (open_ifx == true) {

		s_streamifx_status.ssp_handle = intel_mid_i2s_open(SSP_USAGE_MODEM);

		intel_mid_i2s_command(s_streamifx_status.ssp_handle,
				SSP_CMD_SET_HW_CONFIG,
				&a_alsa_ifx_stream_settings[device_id]);

		/* Set the Write Callback */
		status = intel_mid_i2s_set_wr_cb(s_streamifx_status.ssp_handle,
			intel_alsa_ifx_dma_playback_complete);
		if (status)
			return -EINVAL;

		/* Set the Default Read Callback */
		status = intel_mid_i2s_set_rd_cb(s_streamifx_status.ssp_handle,
			intel_alsa_ifx_dma_capture_complete);
		if (status)
			return -EINVAL;
	}

	switch (str_info->substream->stream) {
	case SNDRV_PCM_STREAM_PLAYBACK:
		if (intel_mid_i2s_command(s_streamifx_status.ssp_handle,
				SSP_CMD_ALLOC_TX, NULL)) {
			pr_err("ALSA IFX: FCT %s Can not alloc TX DMA Channel\n",
					__func__);
			return -EBUSY;
		}
	break;

	case SNDRV_PCM_STREAM_CAPTURE:
		if (intel_mid_i2s_command(s_streamifx_status.ssp_handle,
				SSP_CMD_ALLOC_RX, NULL)) {
			pr_err("ALSA IFX: FCT %s Can not alloc RX DMA Channel\n",
					__func__);
			return -EBUSY;
		}
	break;

	default:
		WARN(1, "ALSA IFX: FCT %s Bad stream_dir: %d\n",
				__func__, str_info->substream->stream);
		return -EINVAL;
		break;
	}

	return 0;
} /* intel_alsa_ifx_open */

/*
 * intel_alsa_ifx_close - This function closes the requested stream
 * The Intel I2S driver is closed only if all streams are closed
 *
 * Input parameters
 *		@str_info : pointer to stream structure
 * Output parameters
 *		@ret_val : status
 */
static int intel_alsa_ifx_close(struct intel_alsa_ifx_stream_info *str_info)
{
	unsigned int device_id;
	bool close_ifx = false;

	WARN(!str_info, "ALSA_IFX: NULL str_info\n");
	if (!str_info)
		return -EINVAL;

	WARN(!s_streamifx_status.ssp_handle, "ALSA_IFX: ERROR, trying to close"
					"a stream however ssp_handle is NULL\n");
	if (!s_streamifx_status.ssp_handle)
		return -EINVAL;

	device_id = str_info->device_id;

	/* Algorithm that detects conflicting call of open /close*/
	spin_lock(&s_streamifx_status.lock);

	if (s_streamifx_status.stream_info == 0) {
		spin_unlock(&s_streamifx_status.lock);
		WARN(1, "ALSA IFX: Close before Open\n");
		return -EBUSY;
	}

	clear_bit(str_info->stream_index, &s_streamifx_status.stream_info);

	if (s_streamifx_status.stream_info == 0)
		close_ifx = true;

	spin_unlock(&s_streamifx_status.lock);

	/*
	 * Release the DMA Channels
	 */

	switch (str_info->substream->stream) {
	case SNDRV_PCM_STREAM_PLAYBACK:
		intel_mid_i2s_command(s_streamifx_status.ssp_handle,
				SSP_CMD_FREE_TX, NULL);

		pr_debug("ALSA IFX: FCT %s TX DMA Channel released\n",
				__func__);
		break;

	case SNDRV_PCM_STREAM_CAPTURE:
		intel_mid_i2s_command(s_streamifx_status.ssp_handle,
				SSP_CMD_FREE_RX, NULL);

		pr_debug("ALSA IFX: FCT %s RX DMA Channel released\n",
				__func__);
		break;

	default:
		WARN(1, "ALSA IFX: FCT %s Bad stream_dir: %d\n",
				__func__, str_info->substream->stream);
		return -EINVAL;
		break;
	}

	clear_bit(INTEL_ALSA_SSP_STREAM_INIT,
			&str_info->stream_status);

	/*
	 * the Close IFX is performed out of lock
	 */
	if (close_ifx) {
		intel_mid_i2s_close(s_streamifx_status.ssp_handle);
		s_streamifx_status.ssp_handle = NULL;
	}

	return 0;
} /* intel_alsa_ifx_close */

/*
 * intel_alsa_ifx_control - Set Control params
 *
 * Input parameters
 *		@command : command to execute
 *		@value: pointer to a structure
 * Output parameters
 *		@ret_val : status
 */
int intel_alsa_ifx_control(int command,
	struct intel_alsa_ifx_stream_info *str_info)
{
	int retval = 0;

	switch (command) {
	case INTEL_ALSA_SSP_CTRL_SND_OPEN:
		retval = intel_alsa_ifx_open(str_info);
		break;
	/*
	 * SND_PAUSE & SND_RESUME not supported in this version
	 */
	case INTEL_ALSA_SSP_CTRL_SND_PAUSE:
	case INTEL_ALSA_SSP_CTRL_SND_RESUME:
		break;

	case INTEL_ALSA_SSP_CTRL_SND_CLOSE:
		intel_alsa_ifx_close(str_info);
	  break;

	default:
		/* Illegal case */
		WARN(1, "ALSA_IFX: intel_alsa_ifx_control Error: Bad Control ID\n");
		return -EINVAL;
		break;
	}
	return retval;
} /* intel_alsa_ifx_control */

/*
 * intel_alsa_ifx_dma_playback_req - This function programs a write request
 * to the Intel I2S driver
 *
 * Input parameters
 *		@str_info : pointer to stream structure
 * Output parameters
 *		@ret_val : status
 */
static int intel_alsa_ifx_dma_playback_req(struct intel_alsa_ifx_stream_info *str_info)
{
	u32 *tx_addr;
	int i;
	int tx_length;
	struct intel_alsa_ssp_dma_buf *sb_tx;
	struct intel_mid_i2s_lli *sg_table;

	WARN(!s_streamifx_status.ssp_handle, "ALSA_SSP: ERROR, trying to play"
					"a stream however ssp_handle is NULL\n");
	if (!s_streamifx_status.ssp_handle)
		return -EINVAL;

	if (!test_bit(INTEL_ALSA_SSP_STREAM_STARTED,
				&str_info->stream_status)) {
		pr_info("%s: Stream has been stopped before SSP"\
				"DMA request has been taken into account",
				__func__);
		return 0;
	}

	if (!test_bit(INTEL_ALSA_SSP_STREAM_INIT, &str_info->stream_status)) {
		sb_tx = &(str_info->dma_slot);
		tx_length = sb_tx->length;

		pr_debug("ALSA_IFX:LLI creqtion of sg list 0x%p\n", tx_addr);

		sg_table = kzalloc(sizeof(struct intel_mid_i2s_lli) *
				sb_tx->period_index_max, GFP_KERNEL);

		if (sg_table == NULL) {
			pr_err("sg_table allocation failed!");
			return -EINVAL;
		}
		pr_debug("ALSA_IFX:LLI sg list addr=0x%p\n", sg_table);

		for (i = 0; i < sb_tx->period_index_max; i++) {
			sg_table[i].addr = (u32 *)(sb_tx->addr +
						tx_length * i);
			sg_table[i].leng = (u32)sb_tx->length;
			pr_debug("ALSA_IFX:LLI element 0x%p(%d) addr:%08x len=%d\n",
					&sg_table[i], i, sg_table[i].addr,
					sg_table[i].leng);
		}
		pr_debug("ALSA_IFX:LLI sg list end of creation, index_max=%d\n",
				sb_tx->period_index_max);

		intel_mid_i2s_lli_wr_req(s_streamifx_status.ssp_handle,
				sg_table, sb_tx->period_index_max,
				I2S_CIRCULAR_MODE, str_info);
		pr_debug("ALSA_IFX:LLI kfree of sg list addr=0x%p\n", sg_table);
		kfree(sg_table);
		intel_mid_i2s_command(s_streamifx_status.ssp_handle,
				SSP_CMD_ENABLE_SSP,
				NULL);
	}

	intel_mid_i2s_command(s_streamifx_status.ssp_handle,
			SSP_CMD_ENABLE_DMA_TX_INTR , NULL);

	return 0;
} /* intel_alsa_ifx_dma_playback_req */

/*
 * intel_alsa_ifx_dma_capture_req - This function programs a read request
 * to the Intel I2S driver
 *
 * Input parameters
 *		@str_info : pointer to stream struct ure
 * Output parameters
 *		@ret_val : status
 */
static int intel_alsa_ifx_dma_capture_req(struct intel_alsa_ifx_stream_info *str_info)
{
	u32 *rx_addr;
	int rx_length;
	struct intel_alsa_ssp_dma_buf *sb_rx;
	struct intel_mid_i2s_lli *sg_table;
	int i;

	WARN(!s_streamifx_status.ssp_handle, "ALSA_IFX: ERROR, trying to play  "
					"a stream however ssp_handle is NULL\n");
	if (!s_streamifx_status.ssp_handle)
		return -EINVAL;


	if (!test_bit(INTEL_ALSA_SSP_STREAM_STARTED,
				&str_info->stream_status)) {
		pr_info("%s: Stream has been stopped before SSP"\
				"DMA request has been taken into account",
				__func__);
		return 0;
	}

	if (!test_bit(INTEL_ALSA_SSP_STREAM_INIT,
				&str_info->stream_status)) {
		sb_rx = &(str_info->dma_slot);
		rx_length = sb_rx->length;

		pr_debug("ALSA_IFX:LLI creqtion of sg list 0x%p\n", rx_addr);

		sg_table = kzalloc(sizeof(struct intel_mid_i2s_lli) *
					sb_rx->period_index_max, GFP_KERNEL);
		pr_debug("ALSA_IFX:LLI sg list addr=0x%p\n", sg_table);
		for (i = 0; i < sb_rx->period_index_max; i++) {
			sg_table[i].addr = (u32 *)(sb_rx->addr +
						rx_length * i);
			sg_table[i].leng = (u32)sb_rx->length;
			pr_debug("ALSA_IFX:LLI element 0x%p(%d) addr:%08x len=%d\n",
				&sg_table[i], i, sg_table[i].addr,
				sg_table[i].leng);
		}
		pr_debug("ALSA_IFX:LLI sg list end of creation, index_max=%d\n",
				sb_rx->period_index_max);

		intel_mid_i2s_lli_rd_req(s_streamifx_status.ssp_handle,
				sg_table, sb_rx->period_index_max,
				I2S_CIRCULAR_MODE, str_info);
		pr_debug("ALSA_IFX:LLI kfree of sg list addr=0x%p\n", sg_table);
		kfree(sg_table);
		intel_mid_i2s_command(s_streamifx_status.ssp_handle,
				SSP_CMD_ENABLE_SSP,
				NULL);
	}

	intel_mid_i2s_command(s_streamifx_status.ssp_handle,
			SSP_CMD_ENABLE_DMA_RX_INTR , NULL);

	return 0;
} /* intel_alsa_ifx_dma_capture_req */

/*
 * intel_alsa_ifx_transfer_data - send data buffers
 *
 * Input parameters
 *		@str_info : pointer to stream structure
 * Output parameters
 *		@ret_val : status
 */
void intel_alsa_ifx_transfer_data(struct work_struct *work)
{
	int status = 0;
	struct intel_alsa_ifx_stream_info *str_info;
	str_info = container_of(work, struct intel_alsa_ifx_stream_info, ssp_ws);

	WARN(!str_info, "ALSA IFX: ERROT NULL str_info\n");

	switch (str_info->substream->stream) {
	case SNDRV_PCM_STREAM_PLAYBACK:
		status = intel_alsa_ifx_dma_playback_req(str_info);
		break;

	case SNDRV_PCM_STREAM_CAPTURE:
		status = intel_alsa_ifx_dma_capture_req(str_info);
		break;

	default:
		WARN(1, "ALSA_IFX: FCT %s Bad stream_dir: %d\n",
				__func__, str_info->substream->stream);
		status = -EINVAL;
		break;
	}
} /* intel_alsa_ifx_transfer_data */

/*
 * intel_alsa_ifx_dma_playback_complete - End of playback callback
 * called in DMA Complete Tasklet context
 * This Callback has in charge of re-programming a new write request to
 * Intel MID I2S Driver if the stream has not been Closed.
 * It calls also the snd_pcm_period_elapsed if the stream is not
 * PAUSED or SUSPENDED to inform ALSA Kernel that the Ring Buffer
 * period has been sent properly
 *
 * Input parameters
 *		@param : pointer to a structure
 * Output parameters
 *		@ret_val : status
 */
int intel_alsa_ifx_dma_playback_complete(void *param)
{
	struct intel_alsa_ifx_stream_info *str_info;
	struct intel_alsa_ssp_dma_buf *sb_tx;
	bool call_back = false;
	bool reset_index = false;

	WARN(!param, "ALSA IFX: ERROR param NULL\n");
	if (!param)
		return -EBUSY;
	str_info = param;
	sb_tx = &(str_info->dma_slot);

	if (++(sb_tx->period_cb_index) >= sb_tx->period_index_max)
		sb_tx->period_cb_index = 0;


	pr_debug("ALSA_IFX:elapsed %d on %d\n",
		sb_tx->period_cb_index, sb_tx->period_index_max);

	if (test_bit(INTEL_ALSA_SSP_STREAM_STARTED, &str_info->stream_status))
		snd_pcm_period_elapsed(str_info->substream);

	return 0;
} /* intel_alsa_ifx_dma_playback_complete */

/*
 * intel_alsa_ifx_dma_capture_complete - End of capture callback
 * called in DMA Complete Tasklet context
 * This Callback has in charge of re-programming a new read request to
 * Intel MID I2S Driver if the stream has not been Closed.
 * It calls also the snd_pcm_period_elapsed if the stream is not
 * PAUSED or SUSPENDED to inform ALSA Kernel that the Ring Buffer
 * period has been received properly
 *
 * Input parameters
 *		@param : pointer to a structure
 * Output parameters
 *		@ret_val : status
 */
int intel_alsa_ifx_dma_capture_complete(void *param)
{
	struct intel_alsa_ssp_dma_buf *sb_rx;
	struct intel_alsa_ifx_stream_info *str_info;
	bool call_back = false;
	bool reset_index = false;

	WARN(!param, "ALSA IFX: ERROR param NULL\n");
	if (!param)
		return -EBUSY;

	str_info = param;
	sb_rx = &(str_info->dma_slot);

	if (++(sb_rx->period_cb_index) >= sb_rx->period_index_max)
		sb_rx->period_cb_index = 0;


	pr_debug("ALSA_IFX:elapsed %d on %d\n",
			sb_rx->period_cb_index, sb_rx->period_index_max);

	if (test_bit(INTEL_ALSA_SSP_STREAM_STARTED, &str_info->stream_status))
		/*
		 * Call the snd_pcm_period_elapsed to inform ALSA kernel
		 * that a ring buffer period has been played
		 */
		snd_pcm_period_elapsed(str_info->substream);

	return 0;
} /* intel_alsa_ifx_dma_capture_complete */

int intel_alsa_ifx_configure_ssp_dma_intr(struct intel_alsa_ifx_stream_info
						*str_info)
{

	WARN(!s_streamifx_status.ssp_handle, "ALSA_IFX:  ERROR, trying  "\
			"to play a stream however ssp_handle is NULL\n");
	if (!s_streamifx_status.ssp_handle)
		return -EINVAL;

	intel_mid_i2s_command(s_streamifx_status.ssp_handle,
		(str_info->substream->stream == SNDRV_PCM_STREAM_PLAYBACK) ?
		SSP_CMD_DISABLE_DMA_TX_INTR :
		SSP_CMD_DISABLE_DMA_RX_INTR, NULL);
	return 0;

}
