/*
 * Copyright (C) 2015 Jared Boone, ShareBrained Technology, Inc.
 * Copyright (C) 2016 Furrtek
 * 
 * This file is part of PortaPack.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */

#include "ui_bht_tx.hpp"
#include "string_format.hpp"

#include "baseband_api.hpp"
#include "portapack_persistent_memory.hpp"

using namespace portapack;

namespace ui {

void BHTView::focus() {
	tx_view.focus();
}

void BHTView::start_tx() {
	baseband::shutdown();
	
	if (tx_type == XYLOS) {
		
		baseband::run_image(portapack::spi_flash::image_tag_tones);
		
		view_xylos.generate_message();
		
		//if (view_xylos.tx_mode == XylosView::tx_modes::SINGLE)
			progressbar.set_max(20);
		//else if (view_xylos.tx_mode == XylosView::tx_modes::SEQUENCE)
		//	progressbar.set_max(20 * XY_SEQ_COUNT);
		
		transmitter_model.set_sampling_rate(TONES_SAMPLERATE);
		transmitter_model.set_rf_amp(true);
		transmitter_model.set_baseband_bandwidth(1750000);
		transmitter_model.enable();
		
		// Setup tones
		for (size_t c = 0; c < ccir_deltas.size(); c++)
			baseband::set_tone(c, ccir_deltas[c], XY_TONE_LENGTH);
		
		baseband::set_tones_config(transmitter_model.channel_bandwidth(), XY_SILENCE, XY_TONE_COUNT, false, false);
		
	} else if (tx_type == EPAR) {
		
		baseband::run_image(portapack::spi_flash::image_tag_ook);
		
		size_t bitstream_length = view_EPAR.generate_message();
		
		progressbar.set_max(2 * 26);
		
		transmitter_model.set_sampling_rate(OOK_SAMPLERATE);
		transmitter_model.set_rf_amp(true);
		transmitter_model.set_baseband_bandwidth(1750000);
		transmitter_model.enable();

		baseband::set_ook_data(
			bitstream_length,
			OOK_SAMPLERATE / 580,
			26,
			encoder_defs[ENCODER_UM3750].pause_symbols
		);
	}
}

void BHTView::on_tx_progress(const uint32_t progress, const bool done) {
	//if (view_xylos.tx_mode == XylosView::tx_modes::SINGLE) {
	
	if (done) {
		transmitter_model.disable();
		view_xylos.tx_mode = XylosView::tx_modes::IDLE;
		tx_view.set_transmitting(false);
	}
	
	if (tx_type == XYLOS) {
		if (done) {
			if (!checkbox_cligno.value()) {
				view_xylos.tx_mode = XylosView::tx_modes::IDLE;
				tx_view.set_transmitting(false);
				progressbar.set_value(0);
			} else {
				chThdSleepMilliseconds(field_tempo.value() * 1000);	// Dirty :(
				
				view_xylos.flip_relays();
				
				start_tx();
			}
		} else
			progressbar.set_value(progress);
	} else if (tx_type == EPAR) {
		if (done) {
			if (!view_EPAR.half) {
				view_EPAR.half = 1;
				start_tx();		// Start second half of transmission
			} else {
				view_EPAR.half = 0;
				progressbar.set_value(0);
				if (checkbox_cligno.value()) {
					chThdSleepMilliseconds(field_tempo.value() * 1000);	// Dirty :(
					
					view_EPAR.flip_relays();
					
					start_tx();
				}
			}
		} else
			progressbar.set_value((26 * view_EPAR.half) + progress);
	}
	
	/*} else if (view_xylos.tx_mode == XylosView::tx_modes::SEQUENCE) {
		if (done) {
			transmitter_model.disable();
			
			if (view_xylos.seq_index < (XY_SEQ_COUNT - 1)) {
				for (c = 0; c < view_xylos.sequence_matin[view_xylos.seq_index].delay; c++)
					chThdSleepMilliseconds(1000);
				
				view_xylos.seq_index++;
				
				start_tx();
			} else {
				progressbar.set_value(0);
				view_xylos.tx_mode = XylosView::tx_modes::IDLE;
				tx_view.set_transmitting(false);
			}
		} else {
			progressbar.set_value((view_xylos.seq_index * 20) + progress);
		}
	}*/
}

BHTView::~BHTView() {
	transmitter_model.disable();
	baseband::shutdown();
}

BHTView::BHTView(NavigationView& nav) {
	add_children({
		&tab_view,
		&labels,
		&view_xylos,
		&view_EPAR,
		&checkbox_cligno,
		&field_tempo,
		&progressbar,
		&text_message,
		&tx_view
	});
	
	field_tempo.set_value(1);
	
	tx_view.on_edit_frequency = [this, &nav]() {
		auto new_view = nav.push<FrequencyKeypadView>(receiver_model.tuning_frequency());
		new_view->on_changed = [this](rf::Frequency f) {
			transmitter_model.set_tuning_frequency(f);
		};
	};
	
	/*button_seq.on_select = [this, &nav](Button&) {
		if (tx_mode == IDLE) {
			seq_index = 0;
			tx_mode = SEQUENCE;
			tx_view.set_transmitting(true);
			start_tx();
		}
	};*/
	
	tx_view.on_start = [this]() {
		if (view_xylos.tx_mode == XylosView::tx_modes::IDLE) {
			view_xylos.tx_mode = XylosView::tx_modes::SINGLE;
			tx_view.set_transmitting(true);
			tx_type = (tx_type_t)tab_view.selected();
			start_tx();
		}
	};
	
	tx_view.on_stop = [this]() {
		transmitter_model.disable();
		tx_view.set_transmitting(false);
		view_xylos.tx_mode = XylosView::tx_modes::IDLE;
	};
}

void EPARView::flip_relays() {
	// Invert first relay's state
	relay_states[0].set_selected_index(relay_states[0].selected_index() ^ 1);
}

size_t EPARView::generate_message() {
	// R2, then R1
	return gen_message_ep(field_city.value(), field_group.selected_index_value(),
							1 - half, relay_states[half].selected_index());
}

EPARView::EPARView(
	Rect parent_rect
) : View(parent_rect) {
	
	hidden(true);
	
	add_children({
		&labels,
		&field_city,
		&field_group,
		//&button_scan
	});

	field_city.set_value(220);
	field_group.set_selected_index(2);
	
	field_city.on_change = [this](int32_t) { generate_message(); };
	field_group.on_change = [this](size_t, int32_t) { generate_message(); };
	
	const auto relay_state_fn = [this](size_t, OptionsField::value_t) {
		this->generate_message();
	};
	
	size_t n = 0;
	for (auto& relay_state : relay_states) {
		relay_state.on_change = relay_state_fn;
		relay_state.set_parent_rect({
			static_cast<Coord>(90 + (n * 36)),
			80,
			24, 24
		});
		relay_state.set_options(relay_options);
		add_child(&relay_state);
		n++;
	}
}

void EPARView::focus() {
	field_city.focus();
}

void XylosView::flip_relays() {
	// Invert first relay's state if not ignored
	size_t rs = relay_states[0].selected_index();
	
	if (rs > 0)
		relay_states[0].set_selected_index(rs ^ 3);
}

void XylosView::generate_message() {
	//if (tx_mode == SINGLE) {
		//text_message.set(
			gen_message_xy(field_header_a.value(), field_header_b.value(), field_city.value(), field_family.value(), 
							checkbox_wcsubfamily.value(), field_subfamily.value(), checkbox_wcid.value(), field_receiver.value(),
							relay_states[0].selected_index(), relay_states[1].selected_index(), 
							relay_states[2].selected_index(), relay_states[3].selected_index());
		//);
	/*} else if (tx_mode == SEQUENCE) {
		//text_message.set(
			gen_message_xy(sequence_matin[seq_index].code);
		//);
	}*/
}

XylosView::XylosView(
	Rect parent_rect
) : View(parent_rect) {
	
	hidden(true);
	
	add_children({
		&labels,
		&field_header_a,
		&field_header_b,
		&field_city,
		&field_family,
		&field_subfamily,
		&checkbox_wcsubfamily,
		&field_receiver,
		&checkbox_wcid,
		//&button_seq,
	});

	field_header_a.set_value(0);
	field_header_b.set_value(0);
	field_city.set_value(10);
	field_family.set_value(1);
	field_subfamily.set_value(1);
	field_receiver.set_value(1);
	
	field_header_a.on_change = [this](int32_t) { generate_message(); };
	field_header_b.on_change = [this](int32_t) { generate_message(); };
	field_city.on_change = [this](int32_t) { generate_message(); };
	field_family.on_change = [this](int32_t) { generate_message(); };
	field_subfamily.on_change = [this](int32_t) { generate_message(); };
	field_receiver.on_change = [this](int32_t) { generate_message(); };
	
	checkbox_wcsubfamily.on_select = [this](Checkbox&, bool v) {
		field_subfamily.set_focusable(!v);
		generate_message();
	};
	
	checkbox_wcid.on_select = [this](Checkbox&, bool v) {
		field_receiver.set_focusable(!v);
		generate_message();
	};
	
	checkbox_wcsubfamily.set_value(true);
	checkbox_wcid.set_value(true);
	
	const auto relay_state_fn = [this](size_t, OptionsField::value_t) {
		this->generate_message();
	};
	
	size_t n = 0;
	for (auto& relay_state : relay_states) {
		relay_state.on_change = relay_state_fn;
		relay_state.set_parent_rect({
			static_cast<Coord>(54 + (n * 36)),
			134,
			24, 24
		});
		relay_state.set_options(relay_options);
		add_child(&relay_state);
		n++;
	}
}

void XylosView::focus() {
	field_city.focus();
}

} /* namespace ui */
